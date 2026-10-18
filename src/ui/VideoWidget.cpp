#include "VideoWidget.h"
#include <QPainter>
#include <QPainterPath>

namespace flux {
VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) { setMinimumSize(320, 180); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); }
void VideoWidget::setFrame(const QImage& image, double pixelAspect) { image_ = image; pixelAspect_ = pixelAspect > 0 ? pixelAspect : 1; update(); }
void VideoWidget::clear() { image_ = {}; update(); }
void VideoWidget::mouseDoubleClickEvent(QMouseEvent*) { emit doubleClicked(); }
void VideoWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.fillRect(rect(), QColor("#080c13"));
    painter.setRenderHint(QPainter::Antialiasing);
    if (!image_.isNull()) {
        QSizeF size(image_.width() * pixelAspect_, image_.height()); size.scale(QSizeF(this->size()), Qt::KeepAspectRatio);
        const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(target, image_); return;
    }
    const QPointF center(width() / 2.0, height() / 2.0 - 34);
    painter.setPen(QPen(QColor("#21312e"), 1)); painter.setBrush(QColor("#10221f")); painter.drawEllipse(center, 43, 43);
    QPainterPath play; play.moveTo(center.x() - 9, center.y() - 15); play.lineTo(center.x() + 17, center.y()); play.lineTo(center.x() - 9, center.y() + 15); play.closeSubpath();
    painter.fillPath(play, QColor("#72e4bb"));
    painter.setPen(QColor("#e2e8ef")); QFont font = painter.font(); font.setPointSize(16); font.setBold(true); painter.setFont(font);
    painter.drawText(QRectF(0, center.y() + 64, width(), 35), Qt::AlignCenter, QStringLiteral("每一帧，都有节奏"));
    font.setPointSize(10); font.setBold(false); painter.setFont(font); painter.setPen(QColor("#81909f"));
    painter.drawText(QRectF(0, center.y() + 106, width(), 30), Qt::AlignCenter, QStringLiteral("拖入音视频文件，或打开网络串流"));
    font.setPointSize(9); painter.setFont(font); painter.setPen(QColor("#536371"));
    painter.drawText(QRectF(0, center.y() + 146, width(), 28), Qt::AlignCenter, QStringLiteral("MP4  /  MKV  /  MP3  /  FLAC  /  RTSP  /  RTMP  /  SRT"));
}
}
