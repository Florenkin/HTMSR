// 文件说明：
// 定义中心线提取统一接口和提取结果结构。

#pragma once

#include <memory>

#include <opencv2/core.hpp>

#include "data_model/reconstruction_types.h"

namespace htmsr::algorithms {

struct CenterlineExtractionOutput {
    data_model::Point2DList points;
    cv::Mat previewImage;
    QString diagnosticMessage;
};

class ICenterlineExtractor {
public:
    virtual ~ICenterlineExtractor() = default;
    virtual CenterlineExtractionOutput extract(
        const cv::Mat& image,
        const data_model::ReconstructionConfig& config,
        const cv::Rect& roi) const = 0;
};

using CenterlineExtractorPtr = std::unique_ptr<ICenterlineExtractor>;

}  // namespace htmsr::algorithms
