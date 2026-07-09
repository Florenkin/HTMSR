#pragma once

#include <QLabel>
#include <QWidget>

#include <opencv2/core.hpp>

namespace htmsr::app {

class ImageViewWidget final : public QWidget {
    Q_OBJECT

public:
    /*
        函数功能：构造图像显示控件
        输入：
            parent：Qt 父控件
        输出：
            无
    */
    explicit ImageViewWidget(QWidget* parent = nullptr);

public slots:
    /*
        函数功能：显示 OpenCV 图像
        输入：
            image：待显示的 OpenCV Mat 图像
        输出：
            无
    */
    void setImage(const cv::Mat& image);
    // 清空图像显示区域。
    void clear();

private:
    /*
        函数功能：将 OpenCV Mat 转换为 Qt QImage
        输入：
            image：OpenCV Mat 图像
        输出：
            返回值：可供 QLabel/QPixmap 显示的 QImage
    */
    QImage toQImage(const cv::Mat& image) const;

    QLabel* imageLabel_ = nullptr;
};

} // namespace htmsr::app
