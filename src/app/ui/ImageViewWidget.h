#pragma once

#include <QLabel>
#include <QWidget>

#include <opencv2/core.hpp>

namespace htmsr::app {

class ImageViewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ImageViewWidget(QWidget* parent = nullptr);

public slots:
    void setImage(const cv::Mat& image);
    void clear();

private:
    QImage toQImage(const cv::Mat& image) const;

    QLabel* imageLabel_ = nullptr;
};

} // namespace htmsr::app
