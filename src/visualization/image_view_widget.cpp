// 文件说明：
// 实现简单图像展示控件。

#include "visualization/image_view_widget.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QPixmap>

namespace htmsr::visualization {

ImageViewWidget::ImageViewWidget(const QString& title, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    titleLabel_ = new QLabel(title, this);
    imageLabel_ = new QLabel("暂无内容", this);
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setMinimumSize(400, 260);
    imageLabel_->setStyleSheet("QLabel { background: #0f172a; color: #e2e8f0; border: 1px solid #334155; }");

    layout->addWidget(titleLabel_);
    layout->addWidget(imageLabel_, 1);
}

void ImageViewWidget::setImage(const QImage& image) {
    imageLabel_->setPixmap(QPixmap::fromImage(image).scaled(imageLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ImageViewWidget::setMessage(const QString& message) {
    imageLabel_->setText(message);
    imageLabel_->setPixmap(QPixmap());
}

}  // namespace htmsr::visualization
