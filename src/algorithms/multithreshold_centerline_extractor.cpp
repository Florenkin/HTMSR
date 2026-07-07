// 文件说明：
// 使用多阈值策略提升弱激光条纹场景下的中心线提取稳定性。

#include "algorithms/multithreshold_centerline_extractor.h"

#include <algorithm>
#include <array>

#include <opencv2/imgproc.hpp>

namespace htmsr::algorithms {

CenterlineExtractionOutput MultiThresholdCenterlineExtractor::extract(
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

    cv::Mat preview;
    cv::cvtColor(gray, preview, cv::COLOR_GRAY2BGR);

    const cv::Rect imageRect(0, 0, gray.cols, gray.rows);
    const cv::Rect activeRoi = roi.area() > 0 ? (roi & imageRect) : imageRect;
    if (activeRoi.area() <= 0) {
        output.previewImage = preview;
        output.diagnosticMessage = "ROI 无效";
        return output;
    }

    const std::array<int, 3> thresholds = {
        std::max(10, static_cast<int>(config.bwThr * 0.8)),
        std::max(10, static_cast<int>(config.bwThr)),
        std::max(10, static_cast<int>(config.bwThr * 1.2))
    };

    for (int x = activeRoi.x; x < activeRoi.x + activeRoi.width; ++x) {
        float bestY = -1.f;
        int bestSupport = 0;
        for (const int thresholdValue : thresholds) {
            double weightedSum = 0.0;
            int support = 0;
            for (int y = activeRoi.y; y < activeRoi.y + activeRoi.height; ++y) {
                const int value = gray.at<uchar>(y, x);
                if (value < thresholdValue) {
                    continue;
                }
                weightedSum += static_cast<double>(y) * value;
                ++support;
            }
            if (support > bestSupport && support >= 3) {
                bestSupport = support;
                bestY = static_cast<float>(weightedSum / (support * 255.0));
            }
        }

        if (bestSupport < 3 || bestY < 0.f) {
            continue;
        }

        output.points.emplace_back(static_cast<float>(x), bestY);
        cv::circle(preview, cv::Point(x, static_cast<int>(bestY)), 1, cv::Scalar(255, 0, 255), -1);
    }

    output.previewImage = preview;
    output.diagnosticMessage = QString("MultiThres 提取到 %1 个点").arg(output.points.size());
    return output;
}

}  // namespace htmsr::algorithms
