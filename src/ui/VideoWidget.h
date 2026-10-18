#pragma once
#include <QImage>
#include <QWidget>

namespace flux {
class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void setFrame(const QImage& image, double pixelAspect);
    void clear();
signals:
    void doubleClicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    QImage image_;
    double pixelAspect_ = 1;
};
}
