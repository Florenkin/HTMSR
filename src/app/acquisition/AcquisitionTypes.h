#pragma once

#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace htmsr::app {

// 相机设备状态，用于后续在线采集页面显示设备连接和采集状态。
enum class CameraState {
    Disconnected,
    Connected,
    Streaming,
    Error
};

// 一组同步的左右图像帧，既可来自离线目录，也可来自在线相机采集。
struct FramePair {
    cv::Mat left;
    cv::Mat right;
    std::string leftPath;
    std::string rightPath;
};

// 单个相机设备抽象接口，后续接入工业相机 SDK 时实现该接口。
class ICameraDevice {
public:
    virtual ~ICameraDevice() = default;
    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual CameraState state() const = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
};

// 图像帧来源抽象接口，重建流程可通过该接口兼容离线图片和在线采集。
class IAcquisitionProvider {
public:
    virtual ~IAcquisitionProvider() = default;
    virtual std::string name() const = 0;
    virtual bool hasNext() const = 0;
    virtual FramePair next() = 0;
    virtual void reset() = 0;
};

using AcquisitionProviderPtr = std::unique_ptr<IAcquisitionProvider>;

/*
    函数功能：将相机状态枚举转换为字符串
    输入：
        state：相机设备状态
    输出：
        返回值：状态对应的字符串
*/
std::string toString(CameraState state);

} // namespace htmsr::app
