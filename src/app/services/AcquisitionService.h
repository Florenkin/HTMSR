#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <functional>
#include <vector>

namespace htmsr::app {

class AcquisitionService {
public:
    using ProgressCallback = std::function<void(int currentFrame, int totalFrames)>;

    /*
        函数功能：枚举当前可用的在线采集设备
        输入：
            无
        输出：
            返回值：相机设备信息列表；无相机或 SDK 未启用时返回空列表
    */
    std::vector<CameraDeviceInfo> enumerateDevices() const;

    /*
        函数功能：按双相机配置执行一次采集会话并保存 left/right 图像
        输入：
            config：双相机设备、参数、帧数和输出目录配置
            progressCallback：采集进度回调，可为空
        输出：
            返回值：采集会话保存目录、图像路径、成功帧数和预览图
    */
    AcquisitionSessionResult capture(const StereoCameraConfig& config, ProgressCallback progressCallback = {}) const;

private:
    AcquisitionProviderPtr createProvider(const StereoCameraConfig& config) const;
};

} // namespace htmsr::app
