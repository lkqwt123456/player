#pragma once
#include "core/MediaSession.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <optional>
#include <vector>

namespace flux {
class PlayerController : public QObject {
    Q_OBJECT
public:
    explicit PlayerController(QObject* parent = nullptr, bool silent = false);
    ~PlayerController() override;
    void open(const QString& source);
    void stop();
    void togglePause();
    void seek(double seconds);
    void setVolume(int percent);
    void setFilter(FilterPreset preset);
    bool paused() const { return paused_; }
    bool seekable() const { return info_.seekable; }
    bool audioOutputActive() const { return sink_ != nullptr; }
    double position() const;
    double duration() const { return info_.duration; }
    QString source() const { return source_; }
signals:
    void frameReady(const QImage& image, double pixelAspect);
    void stateChanged(const QString& state);
    void positionChanged(double seconds, double duration);
    void infoChanged(const QString& text);
    void statsChanged(const QString& text);
    void errorOccurred(const QString& message);
    void playbackEnded();
private:
    void tick();
    void begin(double seconds);
    void retire();
    void resetAudio();
    void setupAudio();
    void feedAudio();
    void setState(const QString& state);
    void resetWall(double seconds);
    void recover(const QString& reason);
    double clock() const;
    AudioSpec audioSpec_;
    QAudioFormat format_;
    QAudioDevice device_;
    std::unique_ptr<QAudioSink> sink_;
    QIODevice* audioDevice_ = nullptr;
    std::unique_ptr<MediaSession> session_;
    std::vector<std::unique_ptr<MediaSession>> retired_;
    SessionInfo info_;
    std::optional<VideoFrame> nextVideo_;
    std::optional<AudioChunk> nextAudio_;
    qsizetype audioOffset_ = 0;
    QTimer timer_;
    QElapsedTimer wall_, uptime_;
    double wallBase_ = 0, frozen_ = 0, audioAnchor_ = 0, startAt_ = 0, lastVideoEnd_ = 0, lastAudioEnd_ = 0;
    qint64 writtenFrames_ = 0, reconnectAt_ = -1, lastReport_ = 0, stableSince_ = -1;
    uint64_t dropped_ = 0;
    int retries_ = 0, volume_ = 75;
    bool network_ = false, silent_ = false, paused_ = false, started_ = false, audioClock_ = false, audioEnded_ = false;
    QString source_, state_;
    FilterPreset filter_ = FilterPreset::None;
};
}
