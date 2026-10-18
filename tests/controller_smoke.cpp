#include "player/PlayerController.h"
#include <QCoreApplication>
#include <QTimer>
#include <cmath>
#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv); avformat_network_init(); av_log_set_level(AV_LOG_ERROR);
    if (app.arguments().size() < 2) return 2;
    flux::PlayerController player(nullptr, !app.arguments().contains("--audio"));
    player.setVolume(0); // Exercise the real output clock without an audible test tone.
    int frames = 0, stage = 0; double pausePosition = 0; bool failed = false, usedAudio = false;
    QObject::connect(&player, &flux::PlayerController::frameReady, [&](const QImage& image, double) { if (image.isNull()) failed = true; ++frames; });
    QObject::connect(&player, &flux::PlayerController::errorOccurred, [&](const QString& message) { std::cerr << message.toStdString() << '\n'; failed = true; });
    QObject::connect(&player, &flux::PlayerController::positionChanged, [&](double position, double) {
        if (stage == 0 && position > 0.4 && player.seekable()) {
            usedAudio = player.audioOutputActive();
            stage = 1; player.togglePause(); pausePosition = player.position();
            QTimer::singleShot(250, [&] {
                if (std::abs(player.position() - pausePosition) > 0.025) failed = true;
                player.seek(2.0); // seek while paused, then resume the new session
                QTimer::singleShot(400, [&] {
                    if (!player.paused() || std::abs(player.position() - 2.0) > 0.05) failed = true;
                    stage = 2; player.togglePause();
                });
            });
        }
    });
    QObject::connect(&player, &flux::PlayerController::playbackEnded, [&] {
        const bool ok = !failed && stage == 2 && (app.arguments().contains("--audio-only") || frames > 10) && player.position() > 5.7
            && (!app.arguments().contains("--audio") || usedAudio);
        std::cout << "Controller frames=" << frames << " position=" << player.position() << " stage=" << stage << " hardwareAudio=" << usedAudio << " valid=" << ok << '\n';
        player.stop(); app.exit(ok ? 0 : 1);
    });
    QTimer::singleShot(12000, &app, [&] { std::cerr << "Controller test timed out\n"; app.exit(1); });
    player.open(app.arguments()[1]); return app.exec();
}
