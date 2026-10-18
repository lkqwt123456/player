#pragma once
#include "FFmpegRAII.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace flux {
struct PublishStatus {
    std::string message;
    uint64_t packets = 0, bytes = 0;
    int attempt = 0;
    bool finished = false, failed = false;
};
// Independent stream-copy publisher. It intentionally does not share playback
// packets: pausing/seeking the preview cannot corrupt outgoing timestamps.
class StreamPublisher {
public:
    ~StreamPublisher();
    void start(std::string source, std::string destination, int retries = 3);
    void stop();
    PublishStatus status() const;
private:
    static int interrupt(void* opaque);
    void run(std::string source, std::string destination, int retries);
    void publishOnce(const std::string& source, const std::string& destination);
    void deadline();
    bool waitUntil(int64_t microseconds);
    void message(std::string text);
    std::atomic<bool> stopped_{false};
    std::atomic<int64_t> deadline_{0};
    mutable std::mutex mutex_;
    PublishStatus status_;
    std::thread thread_;
};
}
