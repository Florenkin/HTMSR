#include "app/ui/ZoomableImageView.h"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSizeF>
#include <QWheelEvent>

#include <algorithm>

namespace htmsr::app {

ZoomableImageView::ZoomableImageView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(480, 360);
    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);
}

void ZoomableImageView::setImageFile(const QString& imagePath)
{
    imagePath_ = imagePath;
    image_ = QImage(imagePath);
    fitToView();
}

void ZoomableImageView::clear()
{
    imagePath_.clear();
    image_ = QImage();
    scale_ = 1.0;
    offset_ = QPointF();
    update();
}

void ZoomableImageView::fitToView()
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

void ZoomableImageView::paintEvent(QPaintEvent*)
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

void ZoomableImageView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!image_.isNull()) {
        fitToView();
    }
}

void ZoomableImageView::wheelEvent(QWheelEvent* event)
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

void ZoomableImageView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !image_.isNull()) {
        panning_ = true;
        lastMousePosition_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void ZoomableImageView::mouseMoveEvent(QMouseEvent* event)
{
    if (!panning_) {
        return;
    }

    offset_ += event->pos() - lastMousePosition_;
    lastMousePosition_ = event->pos();
    update();
    event->accept();
}

void ZoomableImageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        panning_ = false;
        unsetCursor();
        event->accept();
    }
}

void ZoomableImageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!image_.isNull() && event->button() == Qt::LeftButton) {
        fitToView();
        event->accept();
    }
}

QPointF ZoomableImageView::viewportCenter() const
{
    return QPointF(width() * 0.5, height() * 0.5);
}

QRectF ZoomableImageView::imageRect() const
{
    if (image_.isNull()) {
        return QRectF();
    }

    const QSizeF scaledSize(image_.width() * scale_, image_.height() * scale_);
    return QRectF(viewportCenter() + offset_ - QPointF(scaledSize.width() * 0.5, scaledSize.height() * 0.5), scaledSize);
}

} // namespace htmsr::app
