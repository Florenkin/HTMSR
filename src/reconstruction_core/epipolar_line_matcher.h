// 文件说明：
// 声明基于双目几何约束的激光中心线匹配器。

#pragma once

#include "reconstruction_core/imatcher.h"

namespace htmsr::reconstruction_core {

class EpipolarLineMatcher final : public IMatcher {
public:
    StereoMatchingOutput match(
        const cv::Mat& leftImage,
        const cv::Mat& rightImage,
        const data_model::StereoFrameManifestRow& manifestRow,
        const data_model::ReconstructionConfig& config,
        const StereoCalibrationData& calibration,
        const algorithms::CenterlineExtractionOutput& leftCenterline,
        const algorithms::CenterlineExtractionOutput& rightCenterline) const override;
};

}  // namespace htmsr::reconstruction_core
