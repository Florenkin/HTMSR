// 文件说明：
// 声明图像展示控件，用于显示原图和调试预览图。

#pragma once

#include <QImage>
#include <QLabel>
#include <QWidget>

namespace htmsr::visualization {

class ImageViewWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImageViewWidget(const QString& title, QWidget* parent = nullptr);

public slots:
    void setImage(const QImage& image);
    void setMessage(const QString& message);

private:
    QLabel* titleLabel_ = nullptr;
    QLabel* imageLabel_ = nullptr;
};

}  // namespace htmsr::visualization
