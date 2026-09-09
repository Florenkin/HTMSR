#include "app/acquisition/HikCameraDevice.h"

#include "core/Logger.h"

#include <MvCameraControl.h>
#include <MvErrorDefine.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace htmsr::app {
namespace {

// 海康 SDK 的字符串字段使用 unsigned char*，这里统一转成标准字符串便于后续处理。
std::string cString(const unsigned char* value)
{
    return value ? reinterpret_cast<const char*>(value) : std::string{};
}

// 将海康 SDK 返回码格式化为十六进制文本，便于对照官方错误码文档排查问题。
std::string errorText(int code)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << code;
    return stream.str();
}

// 从海康 SDK 设备结构中提取公共设备信息，避免 UI 层依赖 SDK 私有类型。
CameraDeviceInfo toDeviceInfo(const MV_CC_DEVICE_INFO& sdkInfo, int index)
{
    CameraDeviceInfo info;
    info.isMock = false;

    if (sdkInfo.nTLayerType == MV_GIGE_DEVICE) {
        info.transportType = "GigE";
        info.serialNumber = cString(sdkInfo.SpecialInfo.stGigEInfo.chSerialNumber);
        info.name = cString(sdkInfo.SpecialInfo.stGigEInfo.chUserDefinedName);
        if (info.name.empty()) {
            info.name = cString(sdkInfo.SpecialInfo.stGigEInfo.chModelName);
        }
    } else if (sdkInfo.nTLayerType == MV_USB_DEVICE) {
        info.transportType = "USB3";
        info.serialNumber = cString(sdkInfo.SpecialInfo.stUsb3VInfo.chSerialNumber);
        info.name = cString(sdkInfo.SpecialInfo.stUsb3VInfo.chUserDefinedName);
        if (info.name.empty()) {
            info.name = cString(sdkInfo.SpecialInfo.stUsb3VInfo.chModelName);
        }
    } else {
        info.transportType = "Other";
        info.name = "Hik Camera";
    }

    if (info.serialNumber.empty()) {
        info.serialNumber = "index_" + std::to_string(index);
    }
    if (info.name.empty()) {
        info.name = "Hik Camera " + std::to_string(index + 1);
    }
    info.id = info.transportType + ":" + info.serialNumber;
    return info;
}

// 每次连接或刷新设备前重新枚举 SDK 设备列表，保证设备 id 与当前系统状态一致。
std::vector<MV_CC_DEVICE_INFO*> enumerateSdkDevices(MV_CC_DEVICE_INFO_LIST& deviceList)
{
    std::memset(&deviceList, 0, sizeof(deviceList));
    const int code = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
    if (code != MV_OK) {
        Logger::instance().error("HikCamera", "MV_CC_EnumDevices failed, code=" + errorText(code));
        return {};
    }

    std::vector<MV_CC_DEVICE_INFO*> devices;
    for (unsigned int i = 0; i < deviceList.nDeviceNum; ++i) {
        if (deviceList.pDeviceInfo[i]) {
            devices.push_back(deviceList.pDeviceInfo[i]);
        }
    }
    return devices;
}

// 将海康 SDK 输出的原始像素缓冲转换为 OpenCV Mat，统一后续算法处理入口。
cv::Mat convertToMat(void* handle, const unsigned char* data, const MV_FRAME_OUT_INFO_EX& frameInfo)
{
    if (!data || frameInfo.nWidth == 0 || frameInfo.nHeight == 0 || frameInfo.nFrameLen == 0) {
        return {};
    }

    if (frameInfo.enPixelType == PixelType_Gvsp_Mono8) {
        return cv::Mat(frameInfo.nHeight, frameInfo.nWidth, CV_8UC1, const_cast<unsigned char*>(data)).clone();
    }
    if (frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed) {
        return cv::Mat(frameInfo.nHeight, frameInfo.nWidth, CV_8UC3, const_cast<unsigned char*>(data)).clone();
    }
    if (frameInfo.enPixelType == PixelType_Gvsp_RGB8_Packed) {
        cv::Mat rgb(frameInfo.nHeight, frameInfo.nWidth, CV_8UC3, const_cast<unsigned char*>(data));
        cv::Mat bgr;
        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
        return bgr;
    }

    std::vector<unsigned char> converted(static_cast<size_t>(frameInfo.nWidth) * frameInfo.nHeight * 3);
    MV_CC_PIXEL_CONVERT_PARAM_EX convertParam{};
    convertParam.nWidth = frameInfo.nWidth;
    convertParam.nHeight = frameInfo.nHeight;
    convertParam.enSrcPixelType = frameInfo.enPixelType;
    convertParam.pSrcData = const_cast<unsigned char*>(data);
    convertParam.nSrcDataLen = frameInfo.nFrameLen;
    convertParam.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
    convertParam.pDstBuffer = converted.data();
    convertParam.nDstBufferSize = static_cast<unsigned int>(converted.size());

    const int code = MV_CC_ConvertPixelTypeEx(handle, &convertParam);
    if (code != MV_OK) {
        Logger::instance().error("HikCamera", "MV_CC_ConvertPixelTypeEx failed, code=" + errorText(code));
        return {};
    }

    cv::Mat bgr(frameInfo.nHeight, frameInfo.nWidth, CV_8UC3, converted.data());
    return bgr.clone();
}

} // namespace

struct HikCameraDevice::Impl {
    CameraDeviceInfo info;
    CameraState state = CameraState::Disconnected;
    void* handle = nullptr;
    std::vector<unsigned char> frameBuffer;
};

/*
    函数功能：枚举当前系统可用的海康工业相机设备
    输入：
        无
    输出：
        返回值：设备基础信息列表，不暴露任何海康 SDK 私有结构
*/
std::vector<CameraDeviceInfo> enumerateHikCameraDevices()
{
    MV_CC_DEVICE_INFO_LIST deviceList{};
    const auto sdkDevices = enumerateSdkDevices(deviceList);
    std::vector<CameraDeviceInfo> devices;
    for (size_t i = 0; i < sdkDevices.size(); ++i) {
        devices.push_back(toDeviceInfo(*sdkDevices[i], static_cast<int>(i)));
    }
    Logger::instance().info("HikCamera", "Enumerated Hik cameras, count=" + std::to_string(devices.size()));
    return devices;
}

/*
    函数功能：构造海康单相机设备适配器
    输入：
        info：设备枚举结果中的公共设备信息
    输出：
        无（构造后保存设备信息，并初始化内部状态对象）
*/
HikCameraDevice::HikCameraDevice(CameraDeviceInfo info)
    : impl_(std::make_unique<Impl>())
{
    impl_->info = std::move(info);
}

// 析构时主动断开设备，保证相机句柄和取流状态被正确回收。
HikCameraDevice::~HikCameraDevice()
{
    disconnect();
}

std::string HikCameraDevice::id() const
{
    return impl_->info.id;
}

std::string HikCameraDevice::name() const
{
    return impl_->info.name;
}

CameraState HikCameraDevice::state() const
{
    return impl_->state;
}

/*
    函数功能：根据保存的设备 id 重新枚举并连接指定海康相机
    输入：
        无
    输出：
        返回值：连接成功返回 true，否则返回 false 并写错误日志
*/
bool HikCameraDevice::connect()
{
    if (impl_->state != CameraState::Disconnected) {
        return true;
    }

    MV_CC_DEVICE_INFO_LIST deviceList{};
    const auto sdkDevices = enumerateSdkDevices(deviceList);
    MV_CC_DEVICE_INFO* matchedDevice = nullptr;
    for (size_t i = 0; i < sdkDevices.size(); ++i) {
        const auto currentInfo = toDeviceInfo(*sdkDevices[i], static_cast<int>(i));
        if (currentInfo.id == impl_->info.id) {
            matchedDevice = sdkDevices[i];
            break;
        }
    }

    if (!matchedDevice) {
        impl_->state = CameraState::Error;
        Logger::instance().error("HikCamera", "Device not found, id=" + impl_->info.id);
        return false;
    }

    int code = MV_CC_CreateHandle(&impl_->handle, matchedDevice);
    if (code != MV_OK) {
        impl_->state = CameraState::Error;
        Logger::instance().error("HikCamera", "MV_CC_CreateHandle failed, id=" + impl_->info.id + ", code=" + errorText(code));
        return false;
    }

    code = MV_CC_OpenDevice(impl_->handle);
    if (code != MV_OK) {
        MV_CC_DestroyHandle(impl_->handle);
        impl_->handle = nullptr;
        impl_->state = CameraState::Error;
        Logger::instance().error("HikCamera", "MV_CC_OpenDevice failed, id=" + impl_->info.id + ", code=" + errorText(code));
        return false;
    }

    impl_->state = CameraState::Connected;
    Logger::instance().info("HikCamera", "Connected device, id=" + impl_->info.id);
    return true;
}

/*
    函数功能：向海康相机下发曝光、增益、触发和缓冲区等基础参数
    输入：
        config：单相机采集参数配置
    输出：
        返回值：函数执行完成返回 true；不支持的节点仅记录 Warning，不中断流程
*/
bool HikCameraDevice::configure(const CameraParameterConfig& config)
{
    if (!impl_->handle) {
        Logger::instance().error("HikCamera", "Configure failed because device is not connected, id=" + impl_->info.id);
        return false;
    }

    // 首版只写入通用 GenICam 节点；某些型号不支持时记录 Warning，但不直接中断连接流程。
    int code = MV_CC_SetFloatValue(impl_->handle, "ExposureTime", static_cast<float>(config.exposureTime));
    if (code != MV_OK) {
        Logger::instance().warning("HikCamera", "Set ExposureTime failed, id=" + impl_->info.id + ", code=" + errorText(code));
    }
    code = MV_CC_SetFloatValue(impl_->handle, "Gain", static_cast<float>(config.gain));
    if (code != MV_OK) {
        Logger::instance().warning("HikCamera", "Set Gain failed, id=" + impl_->info.id + ", code=" + errorText(code));
    }

    code = MV_CC_SetEnumValue(impl_->handle, "TriggerMode", config.useHardwareTrigger ? MV_TRIGGER_MODE_ON : MV_TRIGGER_MODE_OFF);
    if (code != MV_OK) {
        Logger::instance().warning("HikCamera", "Set TriggerMode failed, id=" + impl_->info.id + ", code=" + errorText(code));
    }

    const unsigned int triggerSource = config.useHardwareTrigger
        ? static_cast<unsigned int>(MV_TRIGGER_SOURCE_LINE0 + std::max(0, config.triggerSourceLine))
        : static_cast<unsigned int>(MV_TRIGGER_SOURCE_SOFTWARE);
    code = MV_CC_SetEnumValue(impl_->handle, "TriggerSource", triggerSource);
    if (code != MV_OK) {
        Logger::instance().warning("HikCamera", "Set TriggerSource failed, id=" + impl_->info.id + ", code=" + errorText(code));
    }

    MVCC_INTVALUE_EX payloadSize{};
    code = MV_CC_GetIntValueEx(impl_->handle, "PayloadSize", &payloadSize);
    if (code == MV_OK && payloadSize.nCurValue > 0) {
        impl_->frameBuffer.resize(static_cast<size_t>(payloadSize.nCurValue));
    } else {
        Logger::instance().warning("HikCamera", "Get PayloadSize failed, id=" + impl_->info.id + ", code=" + errorText(code));
    }

    return true;
}

/*
    函数功能：启动海康相机取流
    输入：
        无
    输出：
        返回值：启动成功返回 true，否则返回 false 并将设备状态置为 Error
*/
bool HikCameraDevice::startGrabbing()
{
    if (!impl_->handle) {
        return false;
    }

    const int code = MV_CC_StartGrabbing(impl_->handle);
    if (code != MV_OK) {
        impl_->state = CameraState::Error;
        Logger::instance().error("HikCamera", "MV_CC_StartGrabbing failed, id=" + impl_->info.id + ", code=" + errorText(code));
        return false;
    }

    impl_->state = CameraState::Streaming;
    return true;
}

/*
    函数功能：停止海康相机取流
    输入：
        无
    输出：
        无（函数执行后设备状态回到 Connected）
*/
void HikCameraDevice::stopGrabbing()
{
    if (!impl_->handle || impl_->state != CameraState::Streaming) {
        return;
    }

    const int code = MV_CC_StopGrabbing(impl_->handle);
    if (code != MV_OK) {
        Logger::instance().warning("HikCamera", "MV_CC_StopGrabbing failed, id=" + impl_->info.id + ", code=" + errorText(code));
    }
    impl_->state = CameraState::Connected;
}

/*
    函数功能：从海康相机抓取一帧图像并转换为 OpenCV Mat
    输入：
        timeoutMs：单帧取流超时时间，单位毫秒
    输出：
        返回值：抓取成功时返回图像；失败时返回空 Mat 并写日志
*/
cv::Mat HikCameraDevice::grabFrame(int timeoutMs)
{
    if (!impl_->handle || impl_->state != CameraState::Streaming) {
        Logger::instance().error("HikCamera", "Grab frame failed because device is not streaming, id=" + impl_->info.id);
        return {};
    }
    if (impl_->frameBuffer.empty()) {
        impl_->frameBuffer.resize(64 * 1024 * 1024);
    }

    MV_FRAME_OUT_INFO_EX frameInfo{};
    const int code = MV_CC_GetOneFrameTimeout(
        impl_->handle,
        impl_->frameBuffer.data(),
        static_cast<unsigned int>(impl_->frameBuffer.size()),
        &frameInfo,
        static_cast<unsigned int>(timeoutMs));
    if (code != MV_OK) {
        Logger::instance().warning("HikCamera", "MV_CC_GetOneFrameTimeout failed, id=" + impl_->info.id + ", code=" + errorText(code));
        return {};
    }

    return convertToMat(impl_->handle, impl_->frameBuffer.data(), frameInfo);
}

/*
    函数功能：断开海康相机连接并释放 SDK 句柄
    输入：
        无
    输出：
        无（函数执行后设备状态重置为 Disconnected）
*/
void HikCameraDevice::disconnect()
{
    if (!impl_) {
        return;
    }

    stopGrabbing();
    if (impl_->handle) {
        MV_CC_CloseDevice(impl_->handle);
        MV_CC_DestroyHandle(impl_->handle);
        impl_->handle = nullptr;
    }
    impl_->state = CameraState::Disconnected;
}

} // namespace htmsr::app
