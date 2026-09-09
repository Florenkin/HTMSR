#include "app/ui/ImageViewWidget.h"

#include <QImage>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>

#include <opencv2/imgproc.hpp>

namespace htmsr::app {

ImageViewWidget::ImageViewWidget(QWidget* parent)
    : QWidget(parent)
{
    // QLabel 放入滚动区域中，图像尺寸较大时仍可浏览。
    imageLabel_ = new QLabel;
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setMinimumSize(320, 240);
    imageLabel_->setText(QString::fromUtf8("无图像"));

    auto* scrollArea = new QScrollArea;
    scrollArea->setWidget(imageLabel_);
    scrollArea->setWidgetResizable(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollArea);
}

void ImageViewWidget::setImage(const cv::Mat& image)
{
    if (image.empty()) {
        clear();
        return;
    }

    // 按当前控件大小等比例缩放，避免图像拉伸变形。
    const QImage qImage = toQImage(image);
    imageLabel_->setPixmap(QPixmap::fromImage(qImage).scaled(
        imageLabel_->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}

void ImageViewWidget::clear()
{
    imageLabel_->clear();
    imageLabel_->setText(QString::fromUtf8("无图像"));
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

} // namespace htmsr::app
