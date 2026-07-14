#include "app/acquisition/MockAcquisitionProvider.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <stdexcept>

namespace htmsr::app {
namespace {

cv::Mat createMockImage(int frameIndex, bool left)
{
    cv::Mat image(720, 960, CV_8UC3, cv::Scalar(24, 24, 24));
    const int offset = left ? 0 : 28;

    // 用渐变背景和斜向激光线模拟左右相机画面，方便无相机阶段验证 UI 与保存链路。
    for (int y = 0; y < image.rows; ++y) {
        const int value = std::min(255, 30 + y / 4);
        image.row(y).setTo(cv::Scalar(value, value, value));
    }

    const int x0 = 120 + frameIndex * 8 + offset;
    const int x1 = image.cols - 120 + frameIndex * 3 + offset;
    cv::line(image, cv::Point(x0, 120), cv::Point(x1, image.rows - 140), cv::Scalar(0, 0, 255), 3, cv::LINE_AA);
    cv::putText(
        image,
        left ? "Mock Left" : "Mock Right",
        cv::Point(32, 60),
        cv::FONT_HERSHEY_SIMPLEX,
        1.0,
        cv::Scalar(255, 255, 255),
        2,
        cv::LINE_AA);
    cv::putText(
        image,
        "frame " + std::to_string(frameIndex + 1),
        cv::Point(32, 105),
        cv::FONT_HERSHEY_SIMPLEX,
        0.8,
        cv::Scalar(220, 220, 220),
        2,
        cv::LINE_AA);
    return image;
}

} // namespace

MockAcquisitionProvider::MockAcquisitionProvider(int frameCount)
    : frameCount_(std::max(1, frameCount))
{
}

std::string MockAcquisitionProvider::name() const
{
    return "Mock Stereo Camera";
}

bool MockAcquisitionProvider::hasNext() const
{
    return index_ < frameCount_;
}

FramePair MockAcquisitionProvider::next()
{
    if (!hasNext()) {
        throw std::runtime_error("Mock acquisition provider has no more frames.");
    }

    FramePair pair;
    pair.frameIndex = index_;
    pair.left = createMockImage(index_, true);
    pair.right = createMockImage(index_, false);
    ++index_;
    return pair;
}

void MockAcquisitionProvider::reset()
{
    index_ = 0;
}

} // namespace htmsr::app
