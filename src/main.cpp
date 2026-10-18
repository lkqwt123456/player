#include "ui/MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <cstdio>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("FluxPlayer"); QCoreApplication::setOrganizationName("FluxPlayer"); QCoreApplication::setApplicationVersion("1.0.0");
    QCommandLineParser parser; parser.setApplicationDescription("Qt + FFmpeg multi-threaded media player"); parser.addHelpOption(); parser.addVersionOption();
    parser.addPositionalArgument("source", "Local file or network URL", "[source]");
    parser.addOption({"silent", "Use a silent presentation clock (for headless testing)."});
    parser.addOption({"quit-after", "Quit after the given milliseconds (for smoke tests).", "ms"});
    parser.addOption({"screenshot", "Save a window screenshot after 1500 ms.", "path"}); parser.process(app);
    av_log_set_level(AV_LOG_WARNING); avformat_network_init();
    int result = 0;
    {
        flux::MainWindow window(parser.isSet("silent")); window.show();
        if (!parser.positionalArguments().isEmpty()) window.openSource(parser.positionalArguments().first());
        if (parser.isSet("quit-after")) { bool ok = false; const auto delay = parser.value("quit-after").toInt(&ok); if (ok && delay > 0) QTimer::singleShot(delay, &app, &QApplication::quit); }
        if (parser.isSet("screenshot")) QTimer::singleShot(1500, &window, [&window, &parser] { if (!window.grab().save(parser.value("screenshot"))) std::fprintf(stderr, "Unable to save screenshot\n"); });
        result = app.exec();
    }
    avformat_network_deinit(); return result;
}
