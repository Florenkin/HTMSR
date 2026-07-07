// 文件说明：
// 定义双目三维重建引擎接口和逐帧输出结构。

#pragma once

#include <opencv2/core.hpp>

#include "data_model/reconstruction_types.h"
#include "reconstruction_core/calibration_types.h"
#include "reconstruction_core/imatcher.h"

namespace htmsr::reconstruction_core {

struct ReconstructionFrameOutput {
    data_model::Point3DList points;
    cv::Mat previewImage;
    QString message;
};

class IReconstructionEngine {
public:
    virtual ~IReconstructionEngine() = default;
    virtual ReconstructionFrameOutput reconstruct(
        const cv::Mat& leftImage,
        const cv::Mat& rightImage,
        const data_model::StereoFrameManifestRow& manifestRow,
        const data_model::ReconstructionConfig& config,
        const StereoCalibrationData& calibration,
        const StereoMatchingOutput& matches) const = 0;
};

}  // namespace htmsr::reconstruction_core
