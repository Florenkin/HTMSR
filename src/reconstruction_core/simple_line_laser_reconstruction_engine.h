// 文件说明：
// 旧单目线激光重建接口声明，当前保留供后续兼容迁移参考。

#pragma once

#include "reconstruction_core/ireconstruction_engine.h"

namespace htmsr::reconstruction_core {

class SimpleLineLaserReconstructionEngine final : public IReconstructionEngine {
public:
    ReconstructionFrameOutput reconstruct(
        const cv::Mat& image,
        const data_model::FrameManifestRow& manifestRow,
        const data_model::ReconstructionConfig& config,
        const UuwCalibrationData& calibration,
        const algorithms::CenterlineExtractionOutput& centerline) const override;
};

}  // namespace htmsr::reconstruction_core
