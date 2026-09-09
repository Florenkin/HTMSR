#pragma once

#include "app/acquisition/AcquisitionTypes.h"

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
    void disconnect() override;

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
