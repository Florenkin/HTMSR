// 文件说明：
// 使用梯度峰值近似 Steger 思路提取激光中心线。

#include "algorithms/steger_centerline_extractor.h"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace htmsr::algorithms {

CenterlineExtractionOutput StegerCenterlineExtractor::extract(
    const cv::Mat& image,
    const data_model::ReconstructionConfig& config,
    const cv::Rect& roi) const {
    CenterlineExtractionOutput output;
    if (image.empty()) {
        output.diagnosticMessage = "输入图像为空";
        return output;
    }

    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image.clone();
    }

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(0, 0), 1.2);

    cv::Mat gradX;
    cv::Sobel(blurred, gradX, CV_32F, 1, 0, 3);

    cv::Mat preview;
    cv::cvtColor(gray, preview, cv::COLOR_GRAY2BGR);

    const cv::Rect imageRect(0, 0, gradX.cols, gradX.rows);
    const cv::Rect activeRoi = roi.area() > 0 ? (roi & imageRect) : imageRect;
    if (activeRoi.area() <= 0) {
        output.previewImage = preview;
        output.diagnosticMessage = "ROI 无效";
        return output;
    }

    const float gradientThreshold = static_cast<float>(config.sumThr);
    for (int x = std::max(1, activeRoi.x); x < std::min(gradX.cols - 1, activeRoi.x + activeRoi.width); ++x) {
        float maxAbsGrad = 0.f;
        int bestY = -1;
        for (int y = std::max(1, activeRoi.y); y < std::min(gradX.rows - 1, activeRoi.y + activeRoi.height); ++y) {
            const float value = std::abs(gradX.at<float>(y, x));
            if (value > maxAbsGrad) {
                maxAbsGrad = value;
                bestY = y;
            }
        }

        if (bestY < 0 || maxAbsGrad < gradientThreshold) {
            continue;
        }

        output.points.emplace_back(static_cast<float>(x), static_cast<float>(bestY));
        cv::circle(preview, cv::Point(x, bestY), 1, cv::Scalar(0, 255, 255), -1);
    }

    output.previewImage = preview;
    output.diagnosticMessage = QString("Steger 近似法提取到 %1 个点").arg(output.points.size());
    return output;
}

}  // namespace htmsr::algorithms
