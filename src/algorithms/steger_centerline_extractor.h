// 文件说明：
// 声明 Steger 近似中心线提取器。

#pragma once

#include "algorithms/icenterline_extractor.h"

namespace htmsr::algorithms {

class StegerCenterlineExtractor final : public ICenterlineExtractor {
public:
    CenterlineExtractionOutput extract(
        const cv::Mat& image,
        const data_model::ReconstructionConfig& config,
        const cv::Rect& roi) const override;
};

}  // namespace htmsr::algorithms
