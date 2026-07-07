// 文件说明：
// 声明双目线激光三维重建引擎。

#pragma once

#include "reconstruction_core/ireconstruction_engine.h"

namespace htmsr::reconstruction_core {

class StereoLineLaserReconstructionEngine final : public IReconstructionEngine {
public:
    ReconstructionFrameOutput reconstruct(
        const cv::Mat& leftImage,
        const cv::Mat& rightImage,
        const data_model::StereoFrameManifestRow& manifestRow,
        const data_model::ReconstructionConfig& config,
        const StereoCalibrationData& calibration,
        const StereoMatchingOutput& matches) const override;
};

}  // namespace htmsr::reconstruction_core
