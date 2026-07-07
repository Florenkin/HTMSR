// 文件说明：
// 使用灰度重心法从激光条纹图像中提取中心线点集。

#include "algorithms/gray_centerline_extractor.h"

#include <opencv2/imgproc.hpp>

namespace htmsr::algorithms {

CenterlineExtractionOutput GrayCenterlineExtractor::extract(
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

    const int thresholdValue = static_cast<int>(config.bwThr);
    for (int x = activeRoi.x; x < activeRoi.x + activeRoi.width; ++x) {
        double weightedSum = 0.0;
        double intensitySum = 0.0;
        for (int y = activeRoi.y; y < activeRoi.y + activeRoi.height; ++y) {
            const uchar value = gray.at<uchar>(y, x);
            if (value < thresholdValue) {
                continue;
            }
            weightedSum += static_cast<double>(y) * value;
            intensitySum += value;
        }

        if (intensitySum <= config.sumThr) {
            continue;
        }

        const float centerY = static_cast<float>(weightedSum / intensitySum);
        output.points.emplace_back(static_cast<float>(x), centerY);
        cv::circle(preview, cv::Point(x, static_cast<int>(centerY)), 1, cv::Scalar(0, 255, 0), -1);
    }

    output.previewImage = preview;
    output.diagnosticMessage = QString("Gray 中心法提取到 %1 个点").arg(output.points.size());
    return output;
}

}  // namespace htmsr::algorithms
