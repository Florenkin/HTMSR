// 文件说明：
// 旧 UUW 标定加载接口声明，当前保留供历史兼容参考。

#pragma once

#include "reconstruction_core/icalibration_loader.h"

namespace htmsr::reconstruction_core {

class UuwYamlCalibrationLoader final : public ICalibrationLoader {
public:
    bool load(const QString& filePath, UuwCalibrationData& calibrationData, QString& error) const override;
};

}  // namespace htmsr::reconstruction_core
