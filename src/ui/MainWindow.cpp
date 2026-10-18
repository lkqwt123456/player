#include "MainWindow.h"
#include "VideoWidget.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace flux {
namespace {
QLabel* label(const QString& text, const char* name = nullptr) {
    auto* result = new QLabel(text); if (name) result->setObjectName(QString::fromLatin1(name)); return result;
}
}
MainWindow::MainWindow(bool silent) : player_(this, silent) {
    setWindowTitle(QStringLiteral("Flux Player · FFmpeg + Qt")); resize(1200, 800); setMinimumSize(920, 650); setAcceptDrops(true);
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#root { background: #111720; color: #dbe4ed; }
        QWidget { font-family: 'Microsoft YaHei UI', 'Noto Sans CJK SC', sans-serif; font-size: 12px; color: #dbe4ed; }
        QLabel#brand { color: #80e8c2; font-size: 23px; font-weight: 700; letter-spacing: 3px; }
        QLabel#muted { color: #7e90a3; font-size: 11px; }
        QLabel#title { font-size: 16px; font-weight: 600; }
        QLabel#section { color: #8b9bad; font-size: 11px; letter-spacing: 2px; }
        QLabel#state { color: #80e8c2; background: #1c332e; border-radius: 10px; padding: 5px 11px; }
        QWidget#sidebar { background: #171f2b; border-radius: 12px; }
        QPushButton { background: #202c3b; border: 1px solid #304054; border-radius: 6px; padding: 9px 15px; }
        QPushButton:hover { background: #2c3e50; border-color: #5b7a92; }
        QPushButton:pressed { background: #16232c; }
        QPushButton:disabled { color: #536371; border-color: #253140; }
        QPushButton#primary { background: #75e2ba; color: #0b241c; border: 0; font-weight: 700; }
        QPushButton#primary:hover { background: #a3f4d6; }
        QLineEdit, QComboBox { background: #101722; border: 1px solid #304054; border-radius: 5px; padding: 8px; selection-background-color: #306e5b; }
        QComboBox QAbstractItemView { background: #202c3b; selection-background-color: #306e5b; }
        QSlider::groove:horizontal { height: 4px; background: #304054; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #75e2ba; border-radius: 2px; }
        QSlider::handle:horizontal { width: 12px; background: #d6f9ec; margin: -4px 0; border-radius: 6px; }
        QPlainTextEdit { background: #101722; border: 1px solid #253140; border-radius: 6px; color: #95a8b9; font-size: 11px; padding: 5px; }
        QStatusBar { background: #0d131c; color: #718398; }
        QToolTip { background: #263749; color: #e6f0fa; border: 1px solid #456; padding: 4px; }
    )"));
    auto* root = new QWidget; root->setObjectName("root"); setCentralWidget(root);
    auto* outer = new QVBoxLayout(root); outer->setContentsMargins(24, 22, 24, 16); outer->setSpacing(18);
    header_ = new QWidget; auto* top = new QHBoxLayout(header_); top->setContentsMargins(0, 0, 0, 0);
    auto* brandBox = new QVBoxLayout; brandBox->setSpacing(3); brandBox->addWidget(label("FLUX PLAYER", "brand")); brandBox->addWidget(label("FFMPEG  /  QT  /  C++17", "muted")); top->addLayout(brandBox); top->addStretch();
    auto* open = new QPushButton(QStringLiteral("打开文件  Ctrl+O")); open->setObjectName("primary");
    auto* url = new QPushButton(QStringLiteral("网络串流  Ctrl+L")); top->addWidget(open); top->addWidget(url); outer->addWidget(header_);
    connect(open, &QPushButton::clicked, this, &MainWindow::openFile); connect(url, &QPushButton::clicked, this, &MainWindow::openUrl);
    auto* body = new QHBoxLayout; body->setSpacing(20); outer->addLayout(body, 1);
    auto* stage = new QVBoxLayout; stage->setSpacing(12); body->addLayout(stage, 1);
    auto* heading = new QHBoxLayout; title_ = label(QStringLiteral("准备播放"), "title"); title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    state_ = label(QStringLiteral("就绪"), "state"); heading->addWidget(title_, 1); heading->addWidget(state_); stage->addLayout(heading);
    video_ = new VideoWidget; stage->addWidget(video_, 1);
    progress_ = new QSlider(Qt::Horizontal); progress_->setRange(0, 10000); progress_->setEnabled(false); stage->addWidget(progress_);
    auto* controls = new QHBoxLayout; playButton_ = new QPushButton(QStringLiteral("播放")); playButton_->setObjectName("primary"); playButton_->setMinimumWidth(70);
    auto* stop = new QPushButton(QStringLiteral("停止")); time_ = label("00:00 / --:--", "muted");
    auto* full = new QPushButton(QStringLiteral("全屏")); full->setToolTip(QStringLiteral("双击画面或按 F / Esc"));
    controls->addWidget(playButton_); controls->addWidget(stop); controls->addWidget(time_); controls->addStretch(); controls->addWidget(full); stage->addLayout(controls);
    auto* settings = new QHBoxLayout; settings->addWidget(label(QStringLiteral("视频滤镜"), "muted"));
    auto* filter = new QComboBox; filter->addItems({QStringLiteral("原始画面"), QStringLiteral("黑白"), QStringLiteral("水平镜像"), QStringLiteral("鲜艳"), QStringLiteral("边缘检测")}); settings->addWidget(filter); settings->addStretch();
    settings->addWidget(label(QStringLiteral("音量"), "muted")); volume_ = new QSlider(Qt::Horizontal); volume_->setRange(0, 100); volume_->setFixedWidth(110); settings->addWidget(volume_);
    auto* volumeText = label("75%", "muted"); volumeText->setMinimumWidth(34); settings->addWidget(volumeText); stage->addLayout(settings);
    sidebar_ = new QWidget; sidebar_->setObjectName("sidebar"); sidebar_->setFixedWidth(276); body->addWidget(sidebar_);
    auto* side = new QVBoxLayout(sidebar_); side->setContentsMargins(18, 20, 18, 18); side->setSpacing(13);
    side->addWidget(label(QStringLiteral("媒体信息  /  MEDIA"), "section")); metadata_ = label(QStringLiteral("打开文件后显示\n编码、分辨率与时钟信息"), "muted"); metadata_->setWordWrap(true); metadata_->setMinimumHeight(60); side->addWidget(metadata_);
    side->addSpacing(8); side->addWidget(label(QStringLiteral("网络推流  /  LIVE"), "section"));
    auto* hint = label(QStringLiteral("将当前媒体作为独立源推送到 RTMP 或 SRT 接收端。"), "muted"); hint->setWordWrap(true); side->addWidget(hint);
    destination_ = new QLineEdit; destination_->setPlaceholderText("rtmp://host/live/key"); destination_->setToolTip(QStringLiteral("RTMP: rtmp://host/live/key\nSRT: srt://host:9000?mode=caller")); side->addWidget(destination_);
    publishButton_ = new QPushButton(QStringLiteral("开始推流")); side->addWidget(publishButton_); publishState_ = label(QStringLiteral("未连接"), "muted"); publishState_->setWordWrap(true); side->addWidget(publishState_);
    auto* pushNote = label(QStringLiteral("流复制：RTMP 需要 H.264 + AAC/MP3。\n推流重连时，从源起点重新发送。"), "muted"); pushNote->setWordWrap(true); side->addWidget(pushNote);
    side->addSpacing(10); side->addWidget(label(QStringLiteral("运行日志  /  EVENTS"), "section")); logs_ = new QPlainTextEdit; logs_->setReadOnly(true); logs_->setMaximumBlockCount(200); side->addWidget(logs_, 1);
    statistics_ = label(QStringLiteral("有界缓冲队列  ·  异步解码  ·  音视频同步"), "muted"); outer->addWidget(statistics_);
    statusBar()->showMessage(QStringLiteral("空格 暂停 / 继续    ← → 跳转 5 秒    F 全屏    支持拖放文件"));
    connect(playButton_, &QPushButton::clicked, &player_, &PlayerController::togglePause);
    connect(stop, &QPushButton::clicked, this, [this] { player_.stop(); video_->clear(); });
    connect(full, &QPushButton::clicked, this, &MainWindow::toggleFullscreen); connect(video_, &VideoWidget::doubleClicked, this, &MainWindow::toggleFullscreen);
    connect(progress_, &QSlider::sliderReleased, this, [this] { player_.seek(player_.duration() * progress_->value() / 10000.0); });
    connect(filter, &QComboBox::currentIndexChanged, this, [this](int index) { player_.setFilter(FilterPreset(index)); });
    connect(volume_, &QSlider::valueChanged, this, [this, volumeText](int value) { player_.setVolume(value); volumeText->setText(QString::number(value) + '%'); });
    connect(&player_, &PlayerController::frameReady, video_, &VideoWidget::setFrame);
    connect(&player_, &PlayerController::stateChanged, this, [this](const QString& state) { state_->setText(state); playButton_->setText(player_.paused() || state == QStringLiteral("已停止") ? QStringLiteral("播放") : QStringLiteral("暂停")); log(state); });
    connect(&player_, &PlayerController::positionChanged, this, [this](double seconds, double duration) {
        progress_->setEnabled(player_.seekable()); if (!progress_->isSliderDown()) progress_->setValue(duration > 0 ? int(seconds / duration * 10000) : 0);
        time_->setText(timeText(seconds) + " / " + (duration > 0 ? timeText(duration) : QStringLiteral("直播 / 未知")));
    });
    connect(&player_, &PlayerController::infoChanged, metadata_, &QLabel::setText); connect(&player_, &PlayerController::statsChanged, statistics_, &QLabel::setText);
    connect(&player_, &PlayerController::errorOccurred, this, [this](const QString& text) { log(text); statusBar()->showMessage(text, 12000); });
    connect(publishButton_, &QPushButton::clicked, this, &MainWindow::startPublish);
    publishTimer_.setInterval(400); connect(&publishTimer_, &QTimer::timeout, this, &MainWindow::updatePublish); publishTimer_.start();
    auto shortcut = [this](const QKeySequence& key, auto action) { auto* item = new QShortcut(key, this); connect(item, &QShortcut::activated, this, action); };
    shortcut(QKeySequence::Open, [this] { openFile(); }); shortcut(QKeySequence("Ctrl+L"), [this] { openUrl(); });
    shortcut(QKeySequence(Qt::Key_Space), [this] { if (!destination_->hasFocus()) player_.togglePause(); });
    shortcut(QKeySequence(Qt::Key_Left), [this] { if (!destination_->hasFocus()) player_.seek(player_.position() - 5); });
    shortcut(QKeySequence(Qt::Key_Right), [this] { if (!destination_->hasFocus()) player_.seek(player_.position() + 5); });
    shortcut(QKeySequence(Qt::Key_F), [this] { if (!destination_->hasFocus()) toggleFullscreen(); });
    shortcut(QKeySequence(Qt::Key_Escape), [this] { if (isFullScreen()) toggleFullscreen(); });
    QSettings saved; lastDirectory_ = saved.value("lastDirectory").toString(); volume_->setValue(saved.value("volume", 75).toInt());
    if (saved.contains("geometry")) restoreGeometry(saved.value("geometry").toByteArray());
    log(QStringLiteral("FFmpeg %1 · Qt %2").arg(QString::fromLatin1(av_version_info()), QString::fromLatin1(qVersion())));
}
MainWindow::~MainWindow() { if (publisher_) publisher_->stop(); }
QString MainWindow::timeText(double seconds) {
    const int value = int(std::max(0.0, seconds));
    return value >= 3600 ? QString("%1:%2:%3").arg(value / 3600).arg(value / 60 % 60, 2, 10, QChar('0')).arg(value % 60, 2, 10, QChar('0'))
                        : QString("%1:%2").arg(value / 60, 2, 10, QChar('0')).arg(value % 60, 2, 10, QChar('0'));
}
void MainWindow::log(const QString& message) { logs_->appendPlainText(QDateTime::currentDateTime().toString("hh:mm:ss") + "  " + message); }
void MainWindow::openSource(const QString& source) {
    video_->clear(); player_.open(source); title_->setText(source.contains("://") ? QStringLiteral("网络串流") : QFileInfo(source).fileName());
    title_->setToolTip(source.contains("://") ? QStringLiteral("网络串流") : source);
}
void MainWindow::openFile() {
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择音视频文件"), lastDirectory_, QStringLiteral("音视频 (*.mp4 *.mkv *.mov *.avi *.webm *.flv *.ts *.m3u8 *.mp3 *.wav *.flac *.aac *.ogg *.m4a);;所有文件 (*)"));
    if (!path.isEmpty()) { lastDirectory_ = QFileInfo(path).absolutePath(); openSource(path); }
}
void MainWindow::openUrl() {
    bool ok = false; const auto url = QInputDialog::getText(this, QStringLiteral("打开网络串流"), QStringLiteral("输入 HTTP / HTTPS / RTSP / RTMP / UDP / TCP / SRT 地址"), QLineEdit::Normal, {}, &ok);
    if (ok && !url.trimmed().isEmpty()) openSource(url.trimmed());
}
void MainWindow::toggleFullscreen() {
    const bool full = !isFullScreen(); header_->setVisible(!full); sidebar_->setVisible(!full); statusBar()->setVisible(!full);
    if (full) showFullScreen(); else showNormal();
}
void MainWindow::startPublish() {
    if (publisher_) { publisher_->stop(); publishButton_->setEnabled(false); publishState_->setText(QStringLiteral("正在停止…")); return; }
    if (player_.source().isEmpty()) { log(QStringLiteral("请先打开一个媒体源。")); return; }
    const auto destination = destination_->text().trimmed();
    if (!destination.startsWith("rtmp://") && !destination.startsWith("rtmps://") && !destination.startsWith("srt://")) { log(QStringLiteral("推流地址必须以 rtmp://、rtmps:// 或 srt:// 开头。")); return; }
    publisher_ = std::make_unique<StreamPublisher>();
    publisher_->start(player_.source().toUtf8().toStdString(), destination.toUtf8().toStdString());
    publishButton_->setText(QStringLiteral("停止推流")); destination_->setEnabled(false); publishMessage_.clear(); log(QStringLiteral("启动独立推流管线"));
}
void MainWindow::updatePublish() {
    if (!publisher_) return;
    const auto status = publisher_->status(); const auto message = QString::fromStdString(status.message);
    publishState_->setText(QStringLiteral("%1\n%2 包 · %3 MB · 第 %4 次连接").arg(message).arg(status.packets).arg(double(status.bytes) / 1048576, 0, 'f', 2).arg(status.attempt));
    if (message != publishMessage_) { publishMessage_ = message; log(QStringLiteral("推流：") + message); }
    if (status.finished) { publisher_.reset(); publishButton_->setText(QStringLiteral("开始推流")); publishButton_->setEnabled(true); destination_->setEnabled(true); }
}
void MainWindow::dragEnterEvent(QDragEnterEvent* event) { if (event->mimeData()->hasUrls()) event->acceptProposedAction(); }
void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls(); if (!urls.empty()) { const auto& url = urls.first(); openSource(url.isLocalFile() ? url.toLocalFile() : url.toString()); event->acceptProposedAction(); }
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (publisher_) publisher_->stop();
    player_.stop();
    QSettings saved; saved.setValue("volume", volume_->value()); saved.setValue("lastDirectory", lastDirectory_); if (!isFullScreen()) saved.setValue("geometry", saveGeometry());
    event->accept();
}
}
