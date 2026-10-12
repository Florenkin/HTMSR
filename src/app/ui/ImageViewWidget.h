#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <opencv2/core.hpp>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

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
    // 将当前图像重新适配到窗口。
    void fitToView();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    /*
        函数功能：将 OpenCV Mat 转换为 Qt QImage
        输入：
            image：OpenCV Mat 图像
        输出：
            返回值：可供 QLabel/QPixmap 显示的 QImage
    */
    QImage toQImage(const cv::Mat& image) const;
    QPointF viewportCenter() const;
    QRectF imageRect() const;

    QImage image_;
    double scale_ = 1.0;
    QPointF offset_;
    QPoint lastMousePosition_;
    bool panning_ = false;
};

} // namespace htmsr::app
