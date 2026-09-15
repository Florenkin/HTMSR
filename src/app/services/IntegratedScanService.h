#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <functional>

namespace htmsr::app {

class IntegratedScanService {
public:
    using ProgressCallback = std::function<void(int currentFrame, int totalFrames, const FramePair& frame)>;

    /*
        函数功能：执行振镜与海康双相机联动扫描，并在采集完成后自动重建点云
        输入：
            config：一键扫描重建所需的振镜、相机、标定与重建配置
        输出：
            返回值：包含采集结果、标定复用情况、重建结果和提示信息的工作流结果
    */
    IntegratedWorkflowResult runScanAndReconstruct(const IntegratedScanConfig& config, ProgressCallback progressCallback = {}) const;
};

} // namespace htmsr::app
