// 文件说明：
// 定义双目中心线匹配统一接口和匹配输出结构。

#pragma once

#include <opencv2/core.hpp>

#include "algorithms/icenterline_extractor.h"
#include "data_model/reconstruction_types.h"
#include "reconstruction_core/calibration_types.h"

namespace htmsr::reconstruction_core {

struct StereoMatchingOutput {
    data_model::StereoPointMatchList pairs;
    cv::Mat previewImage;
    QString diagnosticMessage;
};

class IMatcher {
public:
    virtual ~IMatcher() = default;
    virtual StereoMatchingOutput match(
        const cv::Mat& leftImage,
        const cv::Mat& rightImage,
        const data_model::StereoFrameManifestRow& manifestRow,
        const data_model::ReconstructionConfig& config,
        const StereoCalibrationData& calibration,
        const algorithms::CenterlineExtractionOutput& leftCenterline,
        const algorithms::CenterlineExtractionOutput& rightCenterline) const = 0;
};

}  // namespace htmsr::reconstruction_core
