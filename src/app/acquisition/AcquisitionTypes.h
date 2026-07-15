#pragma once

#include "core/Types.h"

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

// 振镜与相机之间的时序关系，决定在线扫描时相机取流是跟随运动还是独立进行。
enum class GalvoSyncMode {
    Async,
    Sync
};

// 扫描方向配置，对应协议中的正向转动和反向转动命令。
enum class GalvoScanDirection {
    Forward,
    Reverse
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

// 振镜扫描参数，覆盖协议中需要控制的同步、时序、电机、激光和电压能力。
struct GalvoScanConfig {
    std::string portName = "COM3";
    int baudRate = 115200;
    int commandTimeoutMs = 500;
    GalvoSyncMode syncMode = GalvoSyncMode::Sync;
    GalvoScanDirection direction = GalvoScanDirection::Forward;
    int captureIntervalMs = 10;
    int continuousCaptureWaitMs = 20;
    double stepAngleDeg = 0.02;
    int autoRotationAngleDeg = 22;
    int forwardSpeedMs = 10;
    int reverseSpeedMs = 10;
    int laserDuty = 100;
    double voltageRangeV = 7.0;
};

// 单条振镜命令执行结果，既保留原始发送帧，也保留可用于排查问题的返回内容。
struct GalvoCommandResult {
    bool success = false;
    std::vector<unsigned char> request;
    std::vector<unsigned char> response;
    std::string message;
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

// 一键自动流程配置，统一承载相机、振镜、标定与重建所需的任务输入。
struct IntegratedScanConfig {
    StereoCameraConfig stereoCamera;
    GalvoScanConfig galvo;
    CalibrationInput calibrationInput;
    std::string calibrationFile = "stereo_calibration.yml";
    LaserExtractionConfig laserConfig;
    ImageRange reconstructionRange;
    double matchDistanceThreshold = 0.5;
    bool forceRecalibration = false;
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

// 自动工作流执行结果，统一返回采集、标定、重建和标定复用情况。
struct IntegratedWorkflowResult {
    AcquisitionSessionResult acquisition;
    CalibrationResult calibration;
    ReconstructionResult reconstruction;
    bool usedExistingCalibration = false;
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
std::string toString(GalvoSyncMode mode);
std::string toString(GalvoScanDirection direction);

} // namespace htmsr::app
