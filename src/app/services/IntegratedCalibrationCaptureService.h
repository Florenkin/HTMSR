#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <functional>

namespace htmsr::app {

class IntegratedCalibrationCaptureService {
public:
    using ProgressCallback = std::function<void(int currentFrame, int totalFrames, const FramePair& frame)>;

    /*
        函数功能：在线采集左右棋盘格图像并直接执行双目标定
        输入：
            config：自动工作流配置，主要使用其中的双相机采集配置和标定输入配置
        输出：
            返回值：包含采集结果、标定结果和提示信息的工作流结果
    */
    IntegratedWorkflowResult run(const IntegratedScanConfig& config, ProgressCallback progressCallback = {}) const;
};

} // namespace htmsr::app
