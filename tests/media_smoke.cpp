#include "core/MediaSession.h"
#include "core/StreamPublisher.h"
#include <QCoreApplication>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv); avformat_network_init(); av_log_set_level(AV_LOG_ERROR);
    const auto args = app.arguments();
    if (args.size() < 2) { std::cerr << "usage: media_smoke source [--seek seconds] [--filter 0..4] [--error] [--cancel] [--publish url]\n"; return 2; }
    auto option = [&](const QString& key, const QString& fallback = {}) { const auto i = args.indexOf(key); return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback; };
    const auto began = std::chrono::steady_clock::now();
    if (args.contains("--publish")) {
        flux::StreamPublisher publisher; publisher.start(args[1].toUtf8().toStdString(), option("--publish").toUtf8().toStdString(), 0);
        while (!publisher.status().finished && std::chrono::steady_clock::now() - began < std::chrono::seconds(20)) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto status = publisher.status(); publisher.stop();
        std::cout << "Publish: " << status.message << ", packets=" << status.packets << ", bytes=" << status.bytes << '\n';
        return status.finished && !status.failed && status.packets > 0 ? 0 : 1;
    }
    flux::SessionConfig config; config.source = args[1].toUtf8().toStdString(); config.network = args[1].contains("://");
    config.startSeconds = option("--seek", "0").toDouble(); config.filter = flux::FilterPreset(option("--filter", "0").toInt()); config.timeoutMs = option("--timeout", "8000").toInt();
    flux::MediaSession session(config); session.start();
    int videoCount = 0, audioCount = 0; int64_t samples = 0;
    double lastVideo = -1, lastAudio = -1, audioEnd = 0;
    bool valid = true;
    while (std::chrono::steady_clock::now() - began < std::chrono::seconds(18)) {
        flux::VideoFrame video;
        while (session.takeVideo(video)) {
            valid &= !video.image.isNull() && video.pts + 0.001 >= lastVideo && video.pts + video.duration > config.startSeconds - 0.001;
            if (config.filter == flux::FilterPreset::Grayscale) {
                const QColor pixel = video.image.pixelColor(video.image.width() / 3, video.image.height() / 3);
                valid &= std::abs(pixel.red() - pixel.green()) < 3 && std::abs(pixel.green() - pixel.blue()) < 3;
            }
            lastVideo = video.pts; ++videoCount;
        }
        flux::AudioChunk audio;
        while (session.takeAudio(audio)) {
            valid &= !audio.data.isEmpty() && audio.data.size() % config.audio.bytesPerFrame() == 0 && audio.pts + 0.002 >= lastAudio;
            const auto frames = audio.data.size() / config.audio.bytesPerFrame(); samples += frames;
            lastAudio = audio.pts; audioEnd = audio.pts + double(frames) / config.audio.rate; ++audioCount;
        }
        const int runMs = option("--run-for", "100").toInt();
        if (args.contains("--cancel") && std::chrono::steady_clock::now() - began > std::chrono::milliseconds(runMs)) session.stop();
        if (session.finished() && session.videoDrained() && session.audioDrained()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto info = session.info(); const bool done = session.finished(); session.stop();
    std::cout << "video=" << videoCount << " audio=" << audioCount << " samples=" << samples << " lastVideo=" << lastVideo
              << " audioEnd=" << audioEnd << " duration=" << info.duration << " error=" << info.error << '\n';
    if (args.contains("--error")) return done && !info.error.empty() ? 0 : 1;
    if (args.contains("--cancel")) return done && std::chrono::steady_clock::now() - began < std::chrono::milliseconds(option("--run-for", "100").toInt() + 3000)
        && (!args.contains("--require-frames") || (videoCount > 0 && audioCount > 0)) ? 0 : 1;
    if (args.contains("--expect-video")) valid &= videoCount == option("--expect-video").toInt();
    return done && valid && info.ready && info.error.empty() && (!info.hasVideo || videoCount > 0) && (!info.hasAudio || audioCount > 0) ? 0 : 1;
}
