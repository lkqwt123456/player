#include "PlayerController.h"
#include <QFileInfo>
#include <QMediaDevices>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace flux {
PlayerController::PlayerController(QObject* parent, bool silent) : QObject(parent), silent_(silent) {
    format_.setSampleRate(48000); format_.setChannelCount(2); format_.setSampleFormat(QAudioFormat::Int16);
    device_ = QMediaDevices::defaultAudioOutput();
    if (!device_.isNull() && !device_.isFormatSupported(format_)) format_ = device_.preferredFormat();
    if (format_.sampleRate() <= 0 || format_.channelCount() <= 0 || format_.sampleFormat() == QAudioFormat::Unknown) {
        format_.setSampleRate(48000); format_.setChannelCount(2); format_.setSampleFormat(QAudioFormat::Int16); silent_ = true;
    }
    audioSpec_.rate = format_.sampleRate(); audioSpec_.channels = format_.channelCount();
    switch (format_.sampleFormat()) {
    case QAudioFormat::Float: audioSpec_.format = AV_SAMPLE_FMT_FLT; break;
    case QAudioFormat::Int32: audioSpec_.format = AV_SAMPLE_FMT_S32; break;
    case QAudioFormat::UInt8: audioSpec_.format = AV_SAMPLE_FMT_U8; break;
    default: audioSpec_.format = AV_SAMPLE_FMT_S16; break;
    }
    if (device_.isNull()) silent_ = true;
    uptime_.start(); wall_.start(); timer_.setInterval(10); timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &PlayerController::tick); timer_.start();
}
PlayerController::~PlayerController() {
    timer_.stop(); stop();
    // All sessions were interrupted before any destructor joins; callbacks and
    // queue aborts make shutdown bounded by the underlying protocol's IO support.
    retired_.clear();
}
void PlayerController::setState(const QString& state) { if (state_ != state) { state_ = state; emit stateChanged(state); } }
void PlayerController::resetWall(double seconds) { wallBase_ = seconds; wall_.restart(); }
double PlayerController::clock() const {
    if (paused_) return frozen_;
    if (!started_) return startAt_;
    if (audioClock_ && sink_)
        return audioAnchor_ + std::min(double(sink_->processedUSecs()) / 1000000.0, double(writtenFrames_) / audioSpec_.rate);
    return wallBase_ + double(wall_.elapsed()) / 1000;
}
double PlayerController::position() const { return std::max(0.0, info_.duration > 0 ? std::min(clock(), info_.duration) : clock()); }
void PlayerController::resetAudio() {
    if (sink_) sink_->reset();
    sink_.reset(); audioDevice_ = nullptr;
    audioClock_ = false; audioEnded_ = false; writtenFrames_ = 0; audioOffset_ = 0; nextAudio_.reset();
}
void PlayerController::retire() {
    if (session_) { session_->stop(); retired_.push_back(std::move(session_)); }
    resetAudio(); nextVideo_.reset();
}
void PlayerController::open(const QString& source) {
    if (source.trimmed().isEmpty()) return;
    source_ = source.trimmed();
    const QUrl url(source_);
    if (url.isLocalFile()) source_ = url.toLocalFile();
    network_ = source_.contains("://") && !QFileInfo::exists(source_);
    paused_ = false; retries_ = 0; reconnectAt_ = -1; begin(0);
}
void PlayerController::begin(double seconds) {
    retire(); info_ = {}; started_ = false; dropped_ = 0; startAt_ = seconds; lastVideoEnd_ = lastAudioEnd_ = seconds;
    frozen_ = seconds; resetWall(seconds); stableSince_ = -1;
    SessionConfig config; config.source = source_.toUtf8().toStdString(); config.network = network_;
    config.startSeconds = seconds; config.audio = audioSpec_; config.filter = filter_;
    session_ = std::make_unique<MediaSession>(std::move(config)); session_->start();
    setState(QStringLiteral("正在打开")); emit positionChanged(seconds, 0);
}
void PlayerController::stop() {
    reconnectAt_ = -1; retire(); info_ = {}; started_ = false; paused_ = false; startAt_ = 0; frozen_ = 0;
    resetWall(0); setState(QStringLiteral("已停止")); emit positionChanged(0, 0);
}
void PlayerController::togglePause() {
    if (state_ == QStringLiteral("播放结束")) { open(source_); return; }
    if (!session_) { if (!source_.isEmpty()) open(source_); return; }
    if (!paused_) { frozen_ = clock(); paused_ = true; if (sink_) sink_->suspend(); setState(QStringLiteral("已暂停")); }
    else { paused_ = false; resetWall(frozen_); if (sink_) sink_->resume(); setState(QStringLiteral("播放中")); }
}
void PlayerController::seek(double seconds) {
    if (!session_ || !info_.seekable) return;
    reconnectAt_ = -1;
    begin(std::clamp(seconds, 0.0, std::max(0.0, info_.duration - 0.05)));
}
void PlayerController::setVolume(int percent) { volume_ = std::clamp(percent, 0, 100); if (sink_) sink_->setVolume(volume_ / 100.0); }
void PlayerController::setFilter(FilterPreset preset) { filter_ = preset; if (session_) session_->setFilter(preset); }
void PlayerController::setupAudio() {
    if (!info_.hasAudio || silent_) return;
    sink_ = std::make_unique<QAudioSink>(device_, format_);
    sink_->setBufferSize(format_.bytesForDuration(160000)); sink_->setVolume(volume_ / 100.0);
    audioDevice_ = sink_->start();
    if (!audioDevice_ || sink_->error() != QAudio::NoError) {
        resetAudio(); emit errorOccurred(QStringLiteral("音频设备不可用，已切换到静音时钟。"));
    }
    if (paused_ && sink_) sink_->suspend();
}
void PlayerController::feedAudio() {
    if (!info_.hasAudio) return;
    const int frameBytes = audioSpec_.bytesPerFrame();
    auto take = [&] {
        if (!nextAudio_) { AudioChunk chunk; if (session_->takeAudio(chunk)) { nextAudio_ = std::move(chunk); audioOffset_ = 0; } }
        if (nextAudio_) lastAudioEnd_ = std::max(lastAudioEnd_, nextAudio_->pts + double(nextAudio_->data.size()) / (frameBytes * audioSpec_.rate));
    };
    take();
    if (!sink_ || !audioDevice_) {
        // A missing output device still consumes PCM at presentation speed.
        for (int i = 0; i < 32 && nextAudio_ && nextAudio_->pts <= clock() + 0.02; ++i) { nextAudio_.reset(); take(); }
        return;
    }
    for (int i = 0; i < 32 && nextAudio_; ++i) {
        qint64 free = sink_->bytesFree(); free -= free % frameBytes;
        if (free <= 0) break;
        if (!audioClock_) {
            if (nextAudio_->pts > clock() + 0.025) break; // preserve a delayed audio start
            audioAnchor_ = nextAudio_->pts; writtenFrames_ = 0; audioClock_ = true;
        }
        const double expected = audioAnchor_ + double(writtenFrames_) / audioSpec_.rate;
        const double actual = nextAudio_->pts + double(audioOffset_ / frameBytes) / audioSpec_.rate;
        const double gap = actual - expected;
        if (gap > 0.05) {
            // Preserve timestamp gaps using bounded silence instead of jumping video.
            const auto frames = std::min<qint64>(free / frameBytes, qint64(std::min(gap, 0.1) * audioSpec_.rate));
            QByteArray silence(frames * frameBytes, audioSpec_.format == AV_SAMPLE_FMT_U8 ? char(-128) : char(0));
            const qint64 wrote = audioDevice_->write(silence);
            if (wrote <= 0) break;
            writtenFrames_ += wrote / frameBytes; continue;
        }
        if (gap < -0.05) {
            const auto skip = std::min<qsizetype>(nextAudio_->data.size() - audioOffset_, qint64(-gap * audioSpec_.rate) * frameBytes);
            audioOffset_ += skip - skip % frameBytes;
        }
        const qint64 count = std::min<qint64>(free, nextAudio_->data.size() - audioOffset_);
        if (count > 0) {
            const qint64 wrote = audioDevice_->write(nextAudio_->data.constData() + audioOffset_, count);
            if (wrote < 0) { const auto now = clock(); resetAudio(); resetWall(now); emit errorOccurred(QStringLiteral("音频输出失败，已切换到静音播放。")); break; }
            if (!wrote) break;
            audioOffset_ += wrote; writtenFrames_ += wrote / frameBytes;
        }
        if (audioOffset_ >= nextAudio_->data.size()) { nextAudio_.reset(); take(); }
    }
}
void PlayerController::recover(const QString& reason) {
    const double resume = info_.seekable ? position() : 0;
    retire(); started_ = false; startAt_ = resume;
    if (network_ && retries_ < 5) {
        const int delay = std::min(8000, 1000 * (1 << retries_++));
        reconnectAt_ = uptime_.elapsed() + delay;
        setState(QStringLiteral("断线重连 %1/5（%2 秒）").arg(retries_).arg(delay / 1000));
        emit errorOccurred(reason + QStringLiteral("；正在自动重连。"));
    } else { setState(QStringLiteral("播放失败")); emit errorOccurred(reason); }
}
void PlayerController::tick() {
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(), [](const auto& item) { return item->finished(); }), retired_.end());
    if (reconnectAt_ >= 0 && uptime_.elapsed() >= reconnectAt_) { reconnectAt_ = -1; begin(startAt_); }
    if (!session_) return;
    const auto metadata = session_->info();
    if (!metadata.error.empty()) { recover(QString::fromStdString(metadata.error)); return; }
    if (!metadata.ready) return;
    if (!info_.ready) {
        info_ = metadata; setupAudio();
        emit positionChanged(startAt_, info_.duration);
        emit infoChanged(QStringLiteral("%1  %2 × %3   |   %4   |   %5")
            .arg(QString::fromStdString(info_.videoCodec).toUpper()).arg(info_.width).arg(info_.height)
            .arg(QString::fromStdString(info_.audioCodec).toUpper())
            .arg(silent_ ? QStringLiteral("静音时钟") : QStringLiteral("音频主时钟")));
    }
    if (!nextVideo_) { VideoFrame frame; if (session_->takeVideo(frame)) nextVideo_ = std::move(frame); }
    if (!nextAudio_ && info_.hasAudio) { AudioChunk chunk; if (session_->takeAudio(chunk)) { nextAudio_ = std::move(chunk); audioOffset_ = 0; } }
    if (!started_) {
        // Do not wait for both streams: a delayed audio track could otherwise
        // deadlock demux against a full video queue before its first packet.
        const bool ready = nextVideo_ || nextAudio_ || (session_->videoDrained() && session_->audioDrained());
        if (!ready) { setState(paused_ ? QStringLiteral("已暂停") : QStringLiteral("正在缓冲")); return; }
        started_ = true; resetWall(startAt_); stableSince_ = uptime_.elapsed();
        // Preview the first frame even when seeking while paused.
        if (nextVideo_) emit frameReady(nextVideo_->image, nextVideo_->aspect);
    }
    if (paused_) return;
    feedAudio();
    if (audioClock_ && !nextAudio_ && session_->audioDrained() && sink_ && sink_->state() == QAudio::IdleState) {
        const auto end = audioAnchor_ + double(writtenFrames_) / audioSpec_.rate;
        audioClock_ = false; audioEnded_ = true; resetWall(end);
    }
    if (sink_ && sink_->error() != QAudio::NoError && sink_->error() != QAudio::UnderrunError) {
        const auto now = clock(); resetAudio(); resetWall(now); emit errorOccurred(QStringLiteral("音频设备已断开，继续静音播放。"));
    }
    const double now = clock();
    std::optional<VideoFrame> display;
    while (nextVideo_ && nextVideo_->pts <= now + 0.015) {
        if (display) ++dropped_;
        lastVideoEnd_ = nextVideo_->pts + nextVideo_->duration; display = std::move(nextVideo_); nextVideo_.reset();
        VideoFrame next; if (session_->takeVideo(next)) nextVideo_ = std::move(next);
    }
    if (display) emit frameReady(display->image, display->aspect);
    const bool audioDone = !info_.hasAudio || (session_->audioDrained() && !nextAudio_ && (!sink_ || audioEnded_ || !audioClock_));
    if (session_->finished() && session_->videoDrained() && !nextVideo_ && audioDone && now + 0.02 >= std::max(lastVideoEnd_, lastAudioEnd_)) {
        if (network_ && info_.duration <= 0) { recover(QStringLiteral("直播连接已结束")); return; }
        frozen_ = info_.duration > 0 ? info_.duration : position(); paused_ = true; setState(QStringLiteral("播放结束"));
        emit positionChanged(frozen_, info_.duration); emit playbackEnded(); return;
    }
    const bool buffering = audioClock_ && sink_ && sink_->state() == QAudio::IdleState && !session_->audioDrained();
    setState(buffering ? QStringLiteral("正在缓冲") : QStringLiteral("播放中"));
    if (stableSince_ >= 0 && uptime_.elapsed() - stableSince_ > 10000) retries_ = 0;
    if (uptime_.elapsed() - lastReport_ >= 200) {
        lastReport_ = uptime_.elapsed(); emit positionChanged(position(), info_.duration);
        emit statsChanged(QStringLiteral("视频缓冲 %1 帧   ·   音频缓冲 %2 KB   ·   丢帧 %3   ·   压缩包 %4 KB")
            .arg(session_->videoBuffered()).arg(session_->audioBuffered() / 1024).arg(dropped_).arg(session_->packetBytes() / 1024));
    }
}
}
