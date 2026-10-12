#pragma once

#include "app/acquisition/AcquisitionTypes.h"
#include "app/acquisition/HardwareTriggeredStereoCapture.h"

namespace htmsr::app {

class HikCameraDevice final : public ICameraDevice {
public:
    /*
        函数功能：构造海康单相机设备适配器
        输入：
            info：设备枚举得到的公共设备信息
        输出：
            无（构造后保存设备 id，实际连接时在 SDK 内部重新枚举并匹配设备）
    */
    explicit HikCameraDevice(CameraDeviceInfo info);
    ~HikCameraDevice() override;

    std::string id() const override;
    std::string name() const override;
    CameraState state() const override;
    bool connect() override;
    bool configure(const CameraParameterConfig& config) override;
    bool startGrabbing() override;
    void stopGrabbing() override;
    cv::Mat grabFrame(int timeoutMs) override;
    CameraFrame grabFrameWithMetadata(int timeoutMs);
    bool validateHardwareTriggerInterval(int intervalMs);
    void disconnect() override;

    // 最近一次配置失败的具体原因；供采集工作流把 SDK 的节点和错误码直接显示给用户。
    std::string lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/*
    函数功能：枚举当前系统可用的海康工业相机设备
    输入：
        无
    输出：
        返回值：设备基础信息列表，不包含 SDK 私有结构
*/
std::vector<CameraDeviceInfo> enumerateHikCameraDevices();

} // namespace htmsr::app
