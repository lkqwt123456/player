#include "MediaSession.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace flux {
MediaSession::MediaSession(SessionConfig config) : config_(std::move(config)), filter_(config_.filter) {}
MediaSession::~MediaSession() { stop(); if (demuxThread_.joinable()) demuxThread_.join(); }
void MediaSession::start() {
    if (demuxThread_.joinable()) throw std::logic_error("A media session can only be started once");
    demuxThread_ = std::thread(&MediaSession::demux, this);
}
void MediaSession::stop() {
    stopped_.store(true);
    videoPackets_.abort(); audioPackets_.abort(); videoFrames_.abort(); audioChunks_.abort();
}
SessionInfo MediaSession::info() const { std::lock_guard<std::mutex> lock(infoMutex_); return info_; }
int MediaSession::interrupt(void* opaque) {
    const auto* self = static_cast<MediaSession*>(opaque);
    const int64_t deadline = self->deadline_.load();
    return self->stopped_.load() || (deadline > 0 && av_gettime_relative() > deadline);
}
void MediaSession::armDeadline() { deadline_.store(av_gettime_relative() + int64_t(config_.timeoutMs) * 1000); }
void MediaSession::fail(const std::string& message) {
    { std::lock_guard<std::mutex> lock(infoMutex_); if (info_.error.empty()) info_.error = message; }
    stop();
}
void MediaSession::demux() {
    InputPtr input;
    try {
        AVFormatContext* raw = avformat_alloc_context();
        if (!raw) throw std::bad_alloc();
        raw->interrupt_callback = {&MediaSession::interrupt, this};
        Dictionary options;
        options.set("rw_timeout", std::to_string(int64_t(config_.timeoutMs) * 1000).c_str());
        if (config_.network) { options.set("analyzeduration", "2000000"); options.set("probesize", "2097152"); }
        if (config_.source.rfind("rtsp", 0) == 0) options.set("rtsp_transport", "tcp");
        armDeadline();
        const int opened = avformat_open_input(&raw, config_.source.c_str(), nullptr, &options.value);
        input.reset(raw);
        avCheck(opened, "Open media");
        armDeadline(); avCheck(avformat_find_stream_info(input.get(), nullptr), "Probe media");
        deadline_.store(0);
        const int videoIndex = av_find_best_stream(input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        const int audioIndex = av_find_best_stream(input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (videoIndex < 0 && audioIndex < 0) throw std::runtime_error("No supported audio or video stream");
        AVStream* video = videoIndex >= 0 ? input->streams[videoIndex] : nullptr;
        AVStream* audio = audioIndex >= 0 ? input->streams[audioIndex] : nullptr;
        CodecPtr videoCodec = video ? openDecoder(video) : CodecPtr{};
        CodecPtr audioCodec = audio ? openDecoder(audio) : CodecPtr{};
        double origin = 0;
        if (input->start_time != AV_NOPTS_VALUE) origin = double(input->start_time) / AV_TIME_BASE;
        else {
            double earliest = std::numeric_limits<double>::max();
            for (auto* stream : {video, audio})
                if (stream && stream->start_time != AV_NOPTS_VALUE)
                    earliest = std::min(earliest, stream->start_time * av_q2d(stream->time_base));
            if (earliest != std::numeric_limits<double>::max()) origin = earliest;
        }
        SessionInfo metadata;
        metadata.ready = true; metadata.hasAudio = audio; metadata.hasVideo = video;
        metadata.duration = input->duration > 0 ? double(input->duration) / AV_TIME_BASE : 0;
        metadata.seekable = metadata.duration > 0 && (!config_.network || (input->pb && (input->pb->seekable & AVIO_SEEKABLE_NORMAL)));
        if (video) { metadata.width = videoCodec->width; metadata.height = videoCodec->height; metadata.videoCodec = avcodec_get_name(videoCodec->codec_id); }
        if (audio) metadata.audioCodec = avcodec_get_name(audioCodec->codec_id);
        if (config_.startSeconds > 0) {
            if (!metadata.seekable) throw std::runtime_error("This input does not support seeking");
            const auto target = int64_t((config_.startSeconds + origin) * AV_TIME_BASE);
            armDeadline();
            avCheck(avformat_seek_file(input.get(), -1, INT64_MIN, target, target, AVSEEK_FLAG_BACKWARD), "Seek media");
            deadline_.store(0);
        }
        { std::lock_guard<std::mutex> lock(infoMutex_); info_ = metadata; }
        if (video) videoThread_ = std::thread(&MediaSession::decodeVideo, this, std::move(videoCodec), video->time_base, av_guess_frame_rate(input.get(), video, nullptr), origin);
        else videoFrames_.close();
        if (audio) audioThread_ = std::thread(&MediaSession::decodeAudio, this, std::move(audioCodec), audio->time_base, origin);
        else audioChunks_.close();
        while (!stopped_.load()) {
            auto packet = makePacket();
            armDeadline();
            const int result = av_read_frame(input.get(), packet.get());
            deadline_.store(0);
            if (result == AVERROR_EOF) break;
            if (result == AVERROR(EAGAIN)) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
            avCheck(result, "Read media");
            const auto bytes = static_cast<std::size_t>(std::max(0, packet->size));
            if (packet->stream_index == videoIndex) { if (!videoPackets_.push(std::move(packet), bytes)) break; }
            else if (packet->stream_index == audioIndex) { if (!audioPackets_.push(std::move(packet), bytes)) break; }
        }
    } catch (const std::exception& e) { if (!stopped_.load()) fail(e.what()); }
    videoPackets_.close(); audioPackets_.close();
    if (videoThread_.joinable()) videoThread_.join();
    if (audioThread_.joinable()) audioThread_.join();
    videoFrames_.close(); audioChunks_.close();
    finished_.store(true);
}
void MediaSession::decode(AVCodecContext* codec, BoundedQueue<PacketPtr>& queue,
                          const std::function<void(AVFrame*)>& receive) {
    auto frame = makeFrame();
    auto drain = [&] {
        while (!stopped_.load()) {
            const int result = avcodec_receive_frame(codec, frame.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
            avCheck(result, "Receive decoded frame"); receive(frame.get()); av_frame_unref(frame.get());
        }
    };
    while (!stopped_.load()) {
        PacketPtr packet;
        if (!queue.popFor(packet, std::chrono::milliseconds(100))) {
            if (queue.drained()) break;
            continue;
        }
        int result = avcodec_send_packet(codec, packet.get());
        if (result == AVERROR(EAGAIN)) { drain(); result = avcodec_send_packet(codec, packet.get()); }
        avCheck(result, "Send compressed packet"); drain();
    }
    if (!stopped_.load()) { avCheck(avcodec_send_packet(codec, nullptr), "Flush decoder"); drain(); }
}
void MediaSession::decodeVideo(CodecPtr codec, AVRational tb, AVRational rate, double origin) {
    try {
        VideoFilter filter;
        SwsPtr scale;
        double nextPts = config_.startSeconds;
        const double duration = rate.num > 0 && rate.den > 0 ? av_q2d(av_inv_q(rate)) : 0.04;
        auto output = [&](AVFrame* frame) {
            const double pts = frame->pts != AV_NOPTS_VALUE ? frame->pts * av_q2d(tb) - origin : nextPts;
            nextPts = pts + duration;
            if (pts + duration <= config_.startSeconds || stopped_.load()) return;
            if (frame->width <= 0 || frame->height <= 0 || int64_t(frame->width) * frame->height > 64 * 1024 * 1024)
                throw std::runtime_error("Video frame dimensions exceed memory limit");
            QImage image(frame->width, frame->height, QImage::Format_RGBA8888);
            if (image.isNull()) throw std::bad_alloc();
            // sws_getCachedContext may free its old pointer, so release before calling.
            scale.reset(sws_getCachedContext(scale.release(), frame->width, frame->height, AVPixelFormat(frame->format),
                frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr));
            if (!scale) throw std::runtime_error("Cannot create pixel converter");
            uint8_t* dest[] = {image.bits(), nullptr, nullptr, nullptr};
            int strides[] = {int(image.bytesPerLine()), 0, 0, 0};
            avCheck(sws_scale(scale.get(), frame->data, frame->linesize, 0, frame->height, dest, strides), "Convert pixels");
            const double sar = frame->sample_aspect_ratio.num > 0 ? av_q2d(frame->sample_aspect_ratio) : 1;
            const auto bytes = static_cast<std::size_t>(image.sizeInBytes());
            if (videoFrames_.push({std::move(image), std::max(0.0, pts), duration, sar}, bytes)) ++decodedVideo_;
        };
        decode(codec.get(), videoPackets_, [&](AVFrame* frame) {
            frame->pts = frame->best_effort_timestamp;
            filter.process(frame, tb, filter_.load(), output);
        });
        if (!stopped_.load()) filter.flush(output);
    } catch (const std::exception& e) { if (!stopped_.load()) fail(e.what()); }
    videoFrames_.close();
}
void MediaSession::decodeAudio(CodecPtr codec, AVRational tb, double origin) {
    try {
        SwrPtr resampler;
        struct Layout { AVChannelLayout value{}; ~Layout() { av_channel_layout_uninit(&value); } } oldLayout, outLayout;
        av_channel_layout_default(&outLayout.value, config_.audio.channels);
        int oldRate = 0, oldFormat = -1;
        double nextPts = config_.startSeconds;
        auto enqueue = [&](QByteArray data, int samples, double pts) {
            data.resize(samples * config_.audio.bytesPerFrame());
            nextPts = pts + double(samples) / config_.audio.rate;
            if (nextPts <= config_.startSeconds || data.isEmpty()) return;
            if (pts < config_.startSeconds) {
                const int trim = std::min(samples, int(std::ceil((config_.startSeconds - pts) * config_.audio.rate)));
                data.remove(0, trim * config_.audio.bytesPerFrame());
                pts += double(trim) / config_.audio.rate;
            }
            if (data.isEmpty()) return;
            const auto bytes = static_cast<std::size_t>(data.size());
            if (audioChunks_.push({std::move(data), std::max(0.0, pts)}, bytes)) ++decodedAudio_;
        };
        decode(codec.get(), audioPackets_, [&](AVFrame* frame) {
            if (frame->sample_rate <= 0 || !av_channel_layout_check(&frame->ch_layout)) throw std::runtime_error("Invalid input audio format");
            if (!resampler || oldRate != frame->sample_rate || oldFormat != frame->format || av_channel_layout_compare(&oldLayout.value, &frame->ch_layout) != 0) {
                SwrContext* raw = nullptr;
                const int result = swr_alloc_set_opts2(&raw, &outLayout.value, config_.audio.format, config_.audio.rate,
                    &frame->ch_layout, AVSampleFormat(frame->format), frame->sample_rate, 0, nullptr);
                resampler.reset(raw); avCheck(result, "Create audio converter");
                avCheck(swr_init(resampler.get()), "Initialize audio converter");
                oldRate = frame->sample_rate; oldFormat = frame->format;
                av_channel_layout_uninit(&oldLayout.value);
                avCheck(av_channel_layout_copy(&oldLayout.value, &frame->ch_layout), "Copy channel layout");
            }
            const int64_t delay = swr_get_delay(resampler.get(), frame->sample_rate);
            const int64_t capacity = av_rescale_rnd(delay + frame->nb_samples, config_.audio.rate, frame->sample_rate, AV_ROUND_UP);
            if (capacity <= 0 || capacity > config_.audio.rate * 10) throw std::runtime_error("Invalid audio frame size");
            QByteArray data(int(capacity) * config_.audio.bytesPerFrame(), Qt::Uninitialized);
            uint8_t* dest = reinterpret_cast<uint8_t*>(data.data());
            const int samples = swr_convert(resampler.get(), &dest, int(capacity), const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
            avCheck(samples, "Resample audio");
            const double pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? nextPts : frame->best_effort_timestamp * av_q2d(tb) - origin - double(delay) / frame->sample_rate;
            enqueue(std::move(data), samples, pts);
        });
        // Delayed samples also belong to the stream and must be emitted at EOF.
        while (resampler && !stopped_.load()) {
            QByteArray tail(4096 * config_.audio.bytesPerFrame(), Qt::Uninitialized);
            uint8_t* dest = reinterpret_cast<uint8_t*>(tail.data());
            const int samples = swr_convert(resampler.get(), &dest, 4096, nullptr, 0);
            avCheck(samples, "Drain audio converter"); if (!samples) break;
            enqueue(std::move(tail), samples, nextPts);
        }
    } catch (const std::exception& e) { if (!stopped_.load()) fail(e.what()); }
    audioChunks_.close();
}
}
