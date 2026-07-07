// 文件说明：
// 定义双目标定加载统一接口。

#pragma once

#include "reconstruction_core/calibration_types.h"

namespace htmsr::reconstruction_core {

class ICalibrationLoader {
public:
    virtual ~ICalibrationLoader() = default;
    virtual bool load(const QString& filePath, StereoCalibrationData& calibrationData, QString& error) const = 0;
};

}  // namespace htmsr::reconstruction_core
