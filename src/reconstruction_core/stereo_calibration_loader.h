// 文件说明：
// 声明双目标定文件加载器。

#pragma once

#include "reconstruction_core/icalibration_loader.h"

namespace htmsr::reconstruction_core {

class StereoCalibrationLoader final : public ICalibrationLoader {
public:
    bool load(const QString& filePath, StereoCalibrationData& calibrationData, QString& error) const override;
};

}  // namespace htmsr::reconstruction_core
