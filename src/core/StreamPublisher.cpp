#include "StreamPublisher.h"
#include <algorithm>
#include <vector>

namespace flux {
namespace {
struct OutputDeleter {
    void operator()(AVFormatContext* p) const {
        if (!p) return;
        if (!(p->oformat->flags & AVFMT_NOFILE) && p->pb) avio_closep(&p->pb);
        avformat_free_context(p);
    }
};
}
StreamPublisher::~StreamPublisher() { stop(); if (thread_.joinable()) thread_.join(); }
void StreamPublisher::start(std::string source, std::string destination, int retries) {
    if (thread_.joinable()) throw std::logic_error("Publisher can only be started once");
    thread_ = std::thread(&StreamPublisher::run, this, std::move(source), std::move(destination), std::max(0, retries));
}
void StreamPublisher::stop() { stopped_.store(true); }
PublishStatus StreamPublisher::status() const { std::lock_guard<std::mutex> lock(mutex_); return status_; }
void StreamPublisher::message(std::string text) { std::lock_guard<std::mutex> lock(mutex_); status_.message = std::move(text); }
int StreamPublisher::interrupt(void* opaque) {
    auto* self = static_cast<StreamPublisher*>(opaque);
    const auto due = self->deadline_.load();
    return self->stopped_.load() || (due && av_gettime_relative() > due);
}
void StreamPublisher::deadline() { deadline_.store(av_gettime_relative() + 8 * AV_TIME_BASE); }
bool StreamPublisher::waitUntil(int64_t target) {
    while (!stopped_.load() && av_gettime_relative() < target)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return !stopped_.load();
}
void StreamPublisher::run(std::string source, std::string destination, int retries) {
    const bool rtmp = destination.rfind("rtmp://", 0) == 0 || destination.rfind("rtmps://", 0) == 0;
    const bool srt = destination.rfind("srt://", 0) == 0;
    if (!rtmp && !srt) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.message = "Destination must use rtmp://, rtmps:// or srt://";
        status_.failed = status_.finished = true; return;
    }
    for (int attempt = 0; attempt <= retries && !stopped_.load(); ++attempt) {
        { std::lock_guard<std::mutex> lock(mutex_); status_.attempt = attempt + 1; }
        try {
            message("Connecting"); publishOnce(source, destination);
            message(stopped_.load() ? "Stopped" : "Finished"); break;
        } catch (const std::exception& e) {
            if (stopped_.load()) { message("Stopped"); break; }
            message(e.what());
            if (attempt == retries) { std::lock_guard<std::mutex> lock(mutex_); status_.failed = true; break; }
            message(std::string("Reconnecting: ") + e.what());
            if (!waitUntil(av_gettime_relative() + (int64_t(1) << std::min(attempt, 3)) * AV_TIME_BASE)) break;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_); status_.finished = true;
    if (stopped_.load()) status_.message = "Stopped";
}
void StreamPublisher::publishOnce(const std::string& source, const std::string& destination) {
    AVFormatContext* rawInput = avformat_alloc_context();
    if (!rawInput) throw std::bad_alloc();
    rawInput->interrupt_callback = {&StreamPublisher::interrupt, this};
    Dictionary options; options.set("rw_timeout", "8000000");
    if (source.rfind("rtsp", 0) == 0) options.set("rtsp_transport", "tcp");
    deadline();
    const int opened = avformat_open_input(&rawInput, source.c_str(), nullptr, &options.value);
    InputPtr input(rawInput); avCheck(opened, "Open publish source");
    deadline(); avCheck(avformat_find_stream_info(input.get(), nullptr), "Probe publish source");
    const bool rtmp = destination.rfind("rtmp", 0) == 0;
    AVFormatContext* rawOutput = nullptr;
    const int created = avformat_alloc_output_context2(&rawOutput, nullptr, rtmp ? "flv" : "mpegts", destination.c_str());
    std::unique_ptr<AVFormatContext, OutputDeleter> output(rawOutput);
    avCheck(created, "Create output muxer");
    if (!output) throw std::bad_alloc();
    output->interrupt_callback = {&StreamPublisher::interrupt, this};
    std::vector<int> mapping(input->nb_streams, -1);
    for (unsigned i = 0; i < input->nb_streams; ++i) {
        const auto* params = input->streams[i]->codecpar;
        if (params->codec_type != AVMEDIA_TYPE_AUDIO && params->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        if (rtmp && ((params->codec_type == AVMEDIA_TYPE_VIDEO && params->codec_id != AV_CODEC_ID_H264) ||
                     (params->codec_type == AVMEDIA_TYPE_AUDIO && params->codec_id != AV_CODEC_ID_AAC && params->codec_id != AV_CODEC_ID_MP3)))
            throw std::runtime_error("RTMP stream-copy requires H.264 video and AAC/MP3 audio; transcode the source first");
        AVStream* stream = avformat_new_stream(output.get(), nullptr);
        if (!stream) throw std::bad_alloc();
        mapping[i] = stream->index;
        avCheck(avcodec_parameters_copy(stream->codecpar, params), "Copy output stream");
        stream->codecpar->codec_tag = 0;
        stream->time_base = input->streams[i]->time_base;
    }
    if (output->nb_streams == 0) throw std::runtime_error("No audio/video streams to publish");
    Dictionary ioOptions; ioOptions.set("rw_timeout", "8000000");
    deadline();
    avCheck(avio_open2(&output->pb, destination.c_str(), AVIO_FLAG_WRITE, &output->interrupt_callback, &ioOptions.value), "Connect publish destination");
    Dictionary muxOptions;
    if (rtmp) muxOptions.set("flvflags", "no_duration_filesize");
    deadline(); avCheck(avformat_write_header(output.get(), &muxOptions.value), "Write stream header");
    deadline_.store(0); message("Streaming");
    const int64_t began = av_gettime_relative();
    int64_t firstDts = AV_NOPTS_VALUE;
    const int64_t origin = input->start_time != AV_NOPTS_VALUE ? input->start_time : 0;
    while (!stopped_.load()) {
        auto packet = makePacket(); deadline();
        const int read = av_read_frame(input.get(), packet.get()); deadline_.store(0);
        if (read == AVERROR_EOF) break;
        if (read == AVERROR(EAGAIN)) { if (!waitUntil(av_gettime_relative() + 5000)) break; continue; }
        avCheck(read, "Read publish packet");
        if (mapping[packet->stream_index] < 0) continue;
        AVStream* inStream = input->streams[packet->stream_index];
        AVStream* outStream = output->streams[mapping[packet->stream_index]];
        const auto stamp = packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
        if (stamp != AV_NOPTS_VALUE) {
            const auto stampUs = av_rescale_q(stamp, inStream->time_base, AV_TIME_BASE_Q);
            if (firstDts == AV_NOPTS_VALUE) firstDts = stampUs;
            if (!waitUntil(began + std::max(int64_t(0), stampUs - firstDts))) break;
        }
        const auto offset = av_rescale_q(origin, AV_TIME_BASE_Q, inStream->time_base);
        if (packet->pts != AV_NOPTS_VALUE) packet->pts -= offset;
        if (packet->dts != AV_NOPTS_VALUE) packet->dts -= offset;
        av_packet_rescale_ts(packet.get(), inStream->time_base, outStream->time_base);
        packet->stream_index = outStream->index; packet->pos = -1;
        const int bytes = packet->size;
        deadline(); avCheck(av_interleaved_write_frame(output.get(), packet.get()), "Write publish packet"); deadline_.store(0);
        std::lock_guard<std::mutex> lock(mutex_); ++status_.packets; status_.bytes += bytes;
    }
    if (!stopped_.load()) { deadline(); avCheck(av_write_trailer(output.get()), "Finish publish stream"); }
    // Keep a finite IO deadline armed during avio_closep as well.
    deadline();
}
}
