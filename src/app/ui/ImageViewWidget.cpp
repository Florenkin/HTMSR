#include "app/ui/ImageViewWidget.h"

#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSizeF>
#include <QWheelEvent>

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace htmsr::app {

ImageViewWidget::ImageViewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);
}

void ImageViewWidget::setImage(const cv::Mat& image)
{
    if (image.empty()) {
        clear();
        return;
    }

    const QImage qImage = toQImage(image);
    const bool shouldFit = image_.isNull() || image_.size() != qImage.size();
    image_ = qImage;

    if (shouldFit) {
        fitToView();
    } else {
        update();
    }
}

void ImageViewWidget::clear()
{
    image_ = QImage();
    scale_ = 1.0;
    offset_ = QPointF();
    update();
}

void ImageViewWidget::fitToView()
{
    offset_ = QPointF();
    if (image_.isNull() || width() <= 0 || height() <= 0) {
        scale_ = 1.0;
        update();
        return;
    }

    const double widthScale = static_cast<double>(width()) / static_cast<double>(image_.width());
    const double heightScale = static_cast<double>(height()) / static_cast<double>(image_.height());
    scale_ = std::clamp(std::min(widthScale, heightScale), 0.05, 8.0);
    update();
}

void ImageViewWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(245, 245, 245));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (image_.isNull()) {
        painter.setPen(QColor(120, 120, 120));
        painter.drawText(rect(), Qt::AlignCenter, QString::fromUtf8("无图像"));
        return;
    }

    painter.drawImage(imageRect(), image_, QRectF(QPointF(0.0, 0.0), QSizeF(image_.size())));
}

void ImageViewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!image_.isNull()) {
        fitToView();
    }
}

void ImageViewWidget::wheelEvent(QWheelEvent* event)
{
    if (image_.isNull()) {
        return;
    }

    const double oldScale = scale_;
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale_ = std::clamp(scale_ * factor, 0.05, 20.0);
    if (scale_ == oldScale) {
        return;
    }

    const QPointF mousePosition = event->pos();
    const QPointF center = viewportCenter();
    const QPointF imagePointUnderMouse = (mousePosition - center - offset_) / oldScale;
    offset_ = mousePosition - center - imagePointUnderMouse * scale_;
    update();
    event->accept();
}

void ImageViewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !image_.isNull()) {
        panning_ = true;
        lastMousePosition_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void ImageViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!panning_) {
        return;
    }

    offset_ += event->pos() - lastMousePosition_;
    lastMousePosition_ = event->pos();
    update();
    event->accept();
}

void ImageViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        panning_ = false;
        unsetCursor();
        event->accept();
    }
}

void ImageViewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!image_.isNull() && event->button() == Qt::LeftButton) {
        fitToView();
        event->accept();
    }
}

QImage ImageViewWidget::toQImage(const cv::Mat& image) const
{
    // Qt 使用 RGB 顺序，OpenCV 彩色图默认是 BGR，需要在这里转换。
    cv::Mat converted;
    if (image.channels() == 1) {
        cv::cvtColor(image, converted, cv::COLOR_GRAY2RGB);
    } else {
        cv::cvtColor(image, converted, cv::COLOR_BGR2RGB);
    }

    return QImage(
        converted.data,
        converted.cols,
        converted.rows,
        static_cast<int>(converted.step),
        QImage::Format_RGB888).copy();
}

QPointF ImageViewWidget::viewportCenter() const
{
    return QPointF(width() * 0.5, height() * 0.5);
}

QRectF ImageViewWidget::imageRect() const
{
    if (image_.isNull()) {
        return QRectF();
    }

    const QSizeF scaledSize(image_.width() * scale_, image_.height() * scale_);
    return QRectF(viewportCenter() + offset_ - QPointF(scaledSize.width() * 0.5, scaledSize.height() * 0.5), scaledSize);
}

} // namespace htmsr::app
