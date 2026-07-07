// 文件说明：
// 声明灰度重心法中心线提取器。

#pragma once

#include "algorithms/icenterline_extractor.h"

namespace htmsr::algorithms {

class GrayCenterlineExtractor final : public ICenterlineExtractor {
public:
    CenterlineExtractionOutput extract(
        const cv::Mat& image,
        const data_model::ReconstructionConfig& config,
        const cv::Rect& roi) const override;
};

}  // namespace htmsr::algorithms
