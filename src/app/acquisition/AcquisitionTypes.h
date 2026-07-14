#pragma once

#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace htmsr::app {

// 相机设备状态，用于在线采集页面显示设备连接和采集状态。
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
    int frameIndex = 0;
};

// 工业相机设备基础信息，公共层不保存任何 SDK 私有结构，避免第三方类型向 UI 和服务层扩散。
struct CameraDeviceInfo {
    std::string id;
    std::string name;
    std::string serialNumber;
    std::string transportType;
    bool isMock = false;
};

// 单台相机采集参数，首版只放在线采集闭环必须使用的曝光、增益、触发和取流超时。
struct CameraParameterConfig {
    double exposureTime = 5000.0;
    double gain = 0.0;
    bool useHardwareTrigger = true;
    int triggerSourceLine = 0;
    int grabTimeoutMs = 1000;
};

// 双相机采集任务配置，服务层根据该对象选择真实海康相机或 Mock 采集源。
struct StereoCameraConfig {
    std::string leftDeviceId;
    std::string rightDeviceId;
    bool useMockProvider = true;
    int frameCount = 1;
    std::string outputDirectory = ".";
    CameraParameterConfig leftParameters;
    CameraParameterConfig rightParameters;
};

// 一次在线采集会话的保存结果，left/right 目录可直接作为离线重建输入。
struct AcquisitionSessionResult {
    std::string sessionDirectory;
    std::string leftDirectory;
    std::string rightDirectory;
    std::vector<std::string> leftImagePaths;
    std::vector<std::string> rightImagePaths;
    cv::Mat lastLeftPreview;
    cv::Mat lastRightPreview;
    int capturedFrameCount = 0;
    int failedFrameCount = 0;
    bool success = false;
    std::string message;
};

// 单个相机设备抽象接口，后续接入工业相机 SDK 时实现该接口。
class ICameraDevice {
public:
    virtual ~ICameraDevice() = default;
    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual CameraState state() const = 0;
    virtual bool connect() = 0;
    virtual bool configure(const CameraParameterConfig& config) = 0;
    virtual bool startGrabbing() = 0;
    virtual void stopGrabbing() = 0;
    virtual cv::Mat grabFrame(int timeoutMs) = 0;
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

using CameraDevicePtr = std::unique_ptr<ICameraDevice>;
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
