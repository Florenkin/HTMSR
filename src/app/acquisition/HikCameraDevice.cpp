#include "app/acquisition/HikCameraDevice.h"

#include "core/Logger.h"

#include <MvCameraControl.h>
#include <MvErrorDefine.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iterator>
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
    stream << "0x" << std::hex << std::uppercase << static_cast<unsigned int>(code);
    if (code == MV_E_PARAMETER) {
        stream << " (Incorrect parameter or unsupported enum entry)";
    } else if (static_cast<unsigned int>(code) == 0x80000007U) {
        stream << " (No data/trigger timeout)";
    } else if (static_cast<unsigned int>(code) == 0x80000106U) {
        stream << " (GenICam node access condition)";
    }
    return stream.str();
}

std::string triggerSourceName(int line)
{
    return "Line" + std::to_string(line);
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
    bool hardwareTriggerConfigured = false;
    std::string lastError;
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
    Logger::instance().debug("HikCamera", "Enumerated Hik cameras, count=" + std::to_string(devices.size()));
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
    Logger::instance().debug("HikCamera", "Connected device, id=" + impl_->info.id);
    return true;
}

/*
    函数功能：向海康相机下发曝光、增益、触发和缓冲区等基础参数
    输入：
        config：单相机采集参数配置
    输出：
    返回值：非关键节点失败时仍可返回 true；触发模式或触发源配置失败时返回 false
*/
bool HikCameraDevice::configure(const CameraParameterConfig& config)
{
    if (!impl_->handle) {
        impl_->lastError = "Camera is not connected, id=" + impl_->info.id;
        Logger::instance().error("HikCamera", "Configure failed because " + impl_->lastError);
        return false;
    }
    impl_->hardwareTriggerConfigured = false;
    impl_->lastError.clear();

    const auto failTriggerConfiguration = [this](const std::string& message) {
        impl_->lastError = message + ", id=" + impl_->info.id;
        Logger::instance().error("HikCamera", impl_->lastError);
        return false;
    };
    std::string triggerSelector;

    if (config.useHardwareTrigger) {
        // 机型可能使用 FrameStart，也可能只提供 FrameBurstStart；后者还必须限定每次触发一帧。
        int code = MV_CC_SetEnumValueByString(impl_->handle, "AcquisitionMode", "Continuous");
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to set AcquisitionMode=Continuous, code=" + errorText(code));
        }
        code = MV_CC_SetEnumValueByString(impl_->handle, "TriggerMode", "Off");
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to disable trigger before selecting the frame trigger, code=" + errorText(code));
        }
        MVCC_ENUMVALUE selectors{};
        code = MV_CC_GetEnumValue(impl_->handle, "TriggerSelector", &selectors);
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to enumerate TriggerSelector, code=" + errorText(code));
        }
        if (selectors.nSupportedNum == 0 || selectors.nSupportedNum > MV_MAX_XML_SYMBOLIC_NUM) {
            return failTriggerConfiguration("Camera reports an invalid TriggerSelector entry count");
        }
        struct SelectorEntry {
            unsigned int value;
            std::string name;
        };
        std::vector<SelectorEntry> entries;
        std::string supportedSelectors;
        for (unsigned int i = 0; i < selectors.nSupportedNum; ++i) {
            MVCC_ENUMENTRY entry{};
            entry.nValue = selectors.nSupportValue[i];
            code = MV_CC_GetEnumEntrySymbolic(impl_->handle, "TriggerSelector", &entry);
            if (code != MV_OK) {
                return failTriggerConfiguration("Failed to read TriggerSelector entry " +
                    std::to_string(entry.nValue) + ", code=" + errorText(code));
            }
            const std::string name(entry.chSymbolic,
                std::find(std::begin(entry.chSymbolic), std::end(entry.chSymbolic), '\0'));
            entries.push_back({entry.nValue, name});
            supportedSelectors += (supportedSelectors.empty() ? "" : ", ") + name;
        }
        Logger::instance().debug("HikCamera", "Supported TriggerSelector entries=[" + supportedSelectors +
            "], id=" + impl_->info.id);
        auto selected = std::find_if(entries.begin(), entries.end(), [](const SelectorEntry& entry) {
            return entry.name == "FrameStart";
        });
        if (selected == entries.end()) {
            selected = std::find_if(entries.begin(), entries.end(), [](const SelectorEntry& entry) {
                return entry.name == "FrameBurstStart";
            });
        }
        if (selected == entries.end()) {
            return failTriggerConfiguration("Camera does not support FrameStart or FrameBurstStart; supported TriggerSelector entries=[" +
                supportedSelectors + "]");
        }
        triggerSelector = selected->name;

        // 清除相机保留的其它触发门控；使用设备枚举值，避免假设各机型的枚举编号一致。
        for (const auto& entry : entries) {
            code = MV_CC_SetEnumValue(impl_->handle, "TriggerSelector", entry.value);
            if (code == MV_OK) {
                code = MV_CC_SetEnumValueByString(impl_->handle, "TriggerMode", "Off");
            }
            if (code != MV_OK) {
                return failTriggerConfiguration("Failed to disable previous trigger selector " +
                    entry.name + ", code=" + errorText(code));
            }
        }
        code = MV_CC_SetEnumValue(impl_->handle, "TriggerSelector", selected->value);
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to set TriggerSelector=" + triggerSelector + ", code=" + errorText(code));
        }
        MVCC_ENUMVALUE actualSelector{};
        code = MV_CC_GetEnumValue(impl_->handle, "TriggerSelector", &actualSelector);
        if (code != MV_OK || actualSelector.nCurValue != selected->value) {
            return failTriggerConfiguration("Failed to verify TriggerSelector=" + triggerSelector + ", code=" + errorText(code));
        }
        MVCC_ENUMVALUE autoExposure{};
        if (MV_CC_GetEnumValue(impl_->handle, "ExposureAuto", &autoExposure) == MV_OK) {
            code = MV_CC_SetEnumValueByString(impl_->handle, "ExposureAuto", "Off");
            if (code != MV_OK) {
                return failTriggerConfiguration("Failed to set ExposureAuto=Off, code=" + errorText(code));
            }
        }
        bool frameRateEnabled = false;
        if (MV_CC_GetBoolValue(impl_->handle, "AcquisitionFrameRateEnable", &frameRateEnabled) == MV_OK) {
            code = MV_CC_SetBoolValue(impl_->handle, "AcquisitionFrameRateEnable", false);
            if (code != MV_OK) {
                return failTriggerConfiguration("Failed to disable acquisition frame rate limit, code=" + errorText(code));
            }
        }
    }

    // 首版只写入通用 GenICam 节点；曝光、增益等非关键节点失败时记录 Warning，
    // 但触发模式和触发源属于硬触发采集的必要条件，配置失败必须中止本次相机配置。
    int code = MV_CC_SetFloatValue(impl_->handle, "ExposureTime", static_cast<float>(config.exposureTime));
    if (code != MV_OK) {
        if (config.useHardwareTrigger) {
            return failTriggerConfiguration("Failed to set hardware trigger ExposureTime, code=" + errorText(code));
        }
        Logger::instance().warning("HikCamera", "Set ExposureTime failed, id=" + impl_->info.id + ", code=" + errorText(code));
    }
    code = MV_CC_SetFloatValue(impl_->handle, "Gain", static_cast<float>(config.gain));
    if (code != MV_OK) {
        Logger::instance().warning("HikCamera", "Set Gain failed, id=" + impl_->info.id + ", code=" + errorText(code));
    }

    if (!config.useHardwareTrigger) {
        // 软件同步测试使用自由取流。无需设置 TriggerSource=Software；部分机型仅在触发开启后允许写该节点。
        code = MV_CC_SetEnumValueByString(impl_->handle, "TriggerMode", "Off");
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to set TriggerMode=Off for free-run capture, code=" + errorText(code));
        }
        Logger::instance().debug("HikCamera", "Free-run capture configured, id=" + impl_->info.id);
    } else {
        if (config.triggerSourceLine < 0) {
            return failTriggerConfiguration("Hardware trigger line must be non-negative");
        }

        // MVS 官方 IO 示例要求先开启触发，再设置触发源和触发沿。先关闭触发会导致部分 USB3 相机
        // 的 TriggerSource 节点不可写并返回 MV_E_GC_ACCESS (0x80000106)。字符串设置也能明确暴露不支持的 LineX。
        code = MV_CC_SetEnumValueByString(impl_->handle, "TriggerMode", "On");
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to set TriggerMode=On, code=" + errorText(code));
        }

        const std::string source = triggerSourceName(config.triggerSourceLine);
        code = MV_CC_SetEnumValueByString(impl_->handle, "TriggerSource", source.c_str());
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to set TriggerSource=" + source + ", code=" + errorText(code));
        }

        code = MV_CC_SetEnumValueByString(impl_->handle, "TriggerActivation", "RisingEdge");
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to set TriggerActivation=RisingEdge, code=" + errorText(code));
        }

        if (triggerSelector == "FrameBurstStart") {
            // 海康面阵相机的 Burst=1 才是单帧触发，不能沿用 MVS 中可能保留的连拍帧数。
            MVCC_INTVALUE_EX burstCount{};
            code = MV_CC_GetIntValueEx(impl_->handle, "AcquisitionBurstFrameCount", &burstCount);
            if (code != MV_OK) {
                return failTriggerConfiguration("Cannot verify single-frame FrameBurstStart: failed to read AcquisitionBurstFrameCount, code=" + errorText(code));
            }
            if (burstCount.nCurValue != 1) {
                code = MV_CC_SetIntValueEx(impl_->handle, "AcquisitionBurstFrameCount", 1);
                if (code != MV_OK) {
                    return failTriggerConfiguration("Failed to set AcquisitionBurstFrameCount=1, code=" + errorText(code));
                }
                code = MV_CC_GetIntValueEx(impl_->handle, "AcquisitionBurstFrameCount", &burstCount);
                if (code != MV_OK || burstCount.nCurValue != 1) {
                    return failTriggerConfiguration("Failed to verify AcquisitionBurstFrameCount=1, code=" + errorText(code));
                }
            }
        }

        code = MV_CC_SetImageNodeNum(impl_->handle, 16);
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to configure 16 SDK image buffer nodes, code=" + errorText(code));
        }
        code = MV_CC_SetGrabStrategy(impl_->handle, MV_GrabStrategy_OneByOne);
        if (code != MV_OK) {
            return failTriggerConfiguration("Failed to set OneByOne grab strategy, code=" + errorText(code));
        }

        impl_->hardwareTriggerConfigured = true;
        Logger::instance().info(
            "HikCamera",
            "Hardware trigger configured: selector=" + triggerSelector +
                (triggerSelector == "FrameBurstStart" ? ", burstFrames=1" : "") + ", source=" + source +
                ", activation=RisingEdge, bufferNodes=16, strategy=OneByOne, id=" + impl_->info.id);
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

std::string HikCameraDevice::lastError() const
{
    return impl_ ? impl_->lastError : "Camera implementation is unavailable.";
}

bool HikCameraDevice::validateHardwareTriggerInterval(int intervalMs)
{
    const auto fail = [this](const std::string& message) {
        impl_->lastError = message + ", id=" + impl_->info.id;
        Logger::instance().error("HikCamera", impl_->lastError);
        return false;
    };
    if (!impl_->handle || !impl_->hardwareTriggerConfigured || intervalMs <= 0) {
        return fail("Hardware trigger timing validation requires a configured camera and a positive interval");
    }
    MVCC_FLOATVALUE exposure{};
    const int exposureCode = MV_CC_GetFloatValue(impl_->handle, "ExposureTime", &exposure);
    if (exposureCode != MV_OK) {
        return fail("Cannot read actual ExposureTime, code=" + errorText(exposureCode));
    }
    if (exposure.fCurValue >= static_cast<float>(intervalMs) * 1000.0f) {
        return fail("Actual camera exposure is not shorter than the trigger interval. Increase interval or lower exposure");
    }
    MVCC_FLOATVALUE resultingRate{};
    int rateCode = MV_CC_GetFloatValue(impl_->handle, "ResultingFrameRate", &resultingRate);
    if (rateCode != MV_OK) {
        rateCode = MV_CC_GetFloatValue(impl_->handle, "AcquisitionResultingFrameRate", &resultingRate);
    }
    const double requestedRate = 1000.0 / intervalMs;
    if (rateCode == MV_OK && resultingRate.fCurValue > 0.0f) {
        if (requestedRate > resultingRate.fCurValue + 0.01) {
            return fail("Trigger rate " + std::to_string(requestedRate) + " fps exceeds camera resulting rate " +
                std::to_string(resultingRate.fCurValue) + " fps. Increase the trigger interval");
        }
        Logger::instance().info("HikCamera", "Trigger timing checked: exposure=" + std::to_string(exposure.fCurValue) +
            "us, requestedRate=" + std::to_string(requestedRate) + "fps, resultingRate=" +
            std::to_string(resultingRate.fCurValue) + "fps, id=" + impl_->info.id);
    } else {
        Logger::instance().warning("HikCamera", "Camera does not report resulting frame rate; readout/USB throughput must be verified on hardware. "
            "Requested trigger rate=" + std::to_string(requestedRate) + "fps, id=" + impl_->info.id);
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
    if (impl_->hardwareTriggerConfigured) {
        // 在取流已经启动后清掉连续预览遗留帧，避免首帧被旧缓存占用。
        const int clearCode = MV_CC_ClearImageBuffer(impl_->handle);
        if (clearCode != MV_OK) {
            impl_->lastError = "MV_CC_ClearImageBuffer failed, id=" + impl_->info.id + ", code=" + errorText(clearCode);
            Logger::instance().error("HikCamera", impl_->lastError);
            stopGrabbing();
            return false;
        }
    }
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

CameraFrame HikCameraDevice::grabFrameWithMetadata(int timeoutMs)
{
    if (!impl_->handle || impl_->state != CameraState::Streaming) {
        throw std::runtime_error("Camera is not streaming, id=" + impl_->info.id);
    }
    MV_FRAME_OUT frame{};
    const int code = MV_CC_GetImageBuffer(impl_->handle, &frame, static_cast<unsigned int>(std::max(0, timeoutMs)));
    if (code == MV_E_NODATA) {
        return {}; // 接收线程自行重试和计时，轮询无数据不刷屏，也不推进采集序号。
    }
    if (code != MV_OK) {
        throw std::runtime_error("MV_CC_GetImageBuffer failed, id=" + impl_->info.id + ", code=" + errorText(code));
    }
    CameraFrame captured;
    try {
        captured.frameNumber = frame.stFrameInfo.nFrameNum;
        captured.triggerIndex = frame.stFrameInfo.nTriggerIndex;
        captured.lostPacketCount = frame.stFrameInfo.nLostPacket;
        captured.hostTimestamp = frame.stFrameInfo.nHostTimeStamp;
        captured.metadataValid = true;
        captured.image = convertToMat(impl_->handle, frame.pBufAddr, frame.stFrameInfo);
    } catch (...) {
        MV_CC_FreeImageBuffer(impl_->handle, &frame);
        throw;
    }
    const int freeCode = MV_CC_FreeImageBuffer(impl_->handle, &frame);
    if (freeCode != MV_OK || captured.image.empty()) {
        throw std::runtime_error("Camera frame conversion/buffer release failed, id=" + impl_->info.id +
            ", code=" + errorText(freeCode));
    }
    return captured;
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
