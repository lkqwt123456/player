#pragma once
#include "BoundedQueue.h"
#include "FFmpegRAII.h"
#include "VideoFilter.h"
#include <QByteArray>
#include <QImage>
#include <atomic>
#include <mutex>
#include <thread>

namespace flux {
struct AudioSpec {
    int rate = 48000, channels = 2;
    AVSampleFormat format = AV_SAMPLE_FMT_S16;
    int bytesPerFrame() const { return channels * av_get_bytes_per_sample(format); }
};
struct VideoFrame { QImage image; double pts = 0, duration = 0.04, aspect = 1; };
struct AudioChunk { QByteArray data; double pts = 0; };
struct SessionInfo {
    bool ready = false, hasVideo = false, hasAudio = false, seekable = false;
    double duration = 0;
    int width = 0, height = 0;
    std::string videoCodec, audioCodec, error;
};
struct SessionConfig {
    std::string source;
    bool network = false;
    double startSeconds = 0;
    int timeoutMs = 8000;
    AudioSpec audio;
    FilterPreset filter = FilterPreset::None;
};
class MediaSession {
public:
    explicit MediaSession(SessionConfig config);
    ~MediaSession();
    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;
    void start();
    void stop(); // nonblocking: interrupt IO and wake all queue waits
    SessionInfo info() const;
    bool finished() const { return finished_.load(); }
    bool videoDrained() const { return videoFrames_.drained(); }
    bool audioDrained() const { return audioChunks_.drained(); }
    bool takeVideo(VideoFrame& out) { return videoFrames_.tryPop(out); }
    bool takeAudio(AudioChunk& out) { return audioChunks_.tryPop(out); }
    std::size_t videoBuffered() const { return videoFrames_.size(); }
    std::size_t audioBuffered() const { return audioChunks_.bytes(); }
    std::size_t packetBytes() const { return videoPackets_.bytes() + audioPackets_.bytes(); }
    void setFilter(FilterPreset preset) { filter_.store(preset); }
    uint64_t decodedVideo() const { return decodedVideo_.load(); }
    uint64_t decodedAudio() const { return decodedAudio_.load(); }
private:
    static int interrupt(void* opaque);
    void armDeadline();
    void fail(const std::string& message);
    void demux();
    void decodeVideo(CodecPtr codec, AVRational tb, AVRational rate, double origin);
    void decodeAudio(CodecPtr codec, AVRational tb, double origin);
    void decode(AVCodecContext* codec, BoundedQueue<PacketPtr>& queue,
                const std::function<void(AVFrame*)>& receive);
    SessionConfig config_;
    mutable std::mutex infoMutex_;
    SessionInfo info_;
    std::atomic<bool> stopped_{false}, finished_{false};
    std::atomic<int64_t> deadline_{0};
    std::atomic<FilterPreset> filter_;
    std::atomic<uint64_t> decodedVideo_{0}, decodedAudio_{0};
    BoundedQueue<PacketPtr> videoPackets_{64, 32 * 1024 * 1024};
    BoundedQueue<PacketPtr> audioPackets_{128, 8 * 1024 * 1024};
    BoundedQueue<VideoFrame> videoFrames_{8, 256 * 1024 * 1024};
    BoundedQueue<AudioChunk> audioChunks_{24, 2 * 1024 * 1024};
    std::thread demuxThread_, videoThread_, audioThread_;
};
}
