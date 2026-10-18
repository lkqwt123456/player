#pragma once
#include "core/StreamPublisher.h"
#include "player/PlayerController.h"
#include <QMainWindow>
#include <QTimer>
#include <memory>

class QLabel;
class QPushButton;
class QSlider;
class QLineEdit;
class QPlainTextEdit;
namespace flux {
class VideoWidget;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool silent = false);
    ~MainWindow() override;
    void openSource(const QString& source);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
private:
    void openFile();
    void openUrl();
    void toggleFullscreen();
    void startPublish();
    void updatePublish();
    void log(const QString& message);
    static QString timeText(double seconds);
    PlayerController player_;
    VideoWidget* video_ = nullptr;
    QWidget* header_ = nullptr;
    QWidget* sidebar_ = nullptr;
    QLabel *title_ = nullptr, *state_ = nullptr, *time_ = nullptr, *metadata_ = nullptr, *statistics_ = nullptr, *publishState_ = nullptr;
    QPushButton *playButton_ = nullptr, *publishButton_ = nullptr;
    QSlider *progress_ = nullptr, *volume_ = nullptr;
    QLineEdit* destination_ = nullptr;
    QPlainTextEdit* logs_ = nullptr;
    QTimer publishTimer_;
    std::unique_ptr<StreamPublisher> publisher_;
    QString lastDirectory_, publishMessage_;
};
}
