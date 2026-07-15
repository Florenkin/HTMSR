#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace htmsr::app {

class IGalvoController {
public:
    virtual ~IGalvoController() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual GalvoCommandResult setSyncMode(GalvoSyncMode mode) = 0;
    virtual GalvoCommandResult getSyncMode(GalvoSyncMode& mode) = 0;

    virtual GalvoCommandResult setCaptureIntervalMs(int intervalMs) = 0;
    virtual GalvoCommandResult getCaptureIntervalMs(int& intervalMs) = 0;

    virtual GalvoCommandResult setContinuousCaptureWaitMs(int intervalMs) = 0;
    virtual GalvoCommandResult getContinuousCaptureWaitMs(int& intervalMs) = 0;

    virtual GalvoCommandResult setStepAngle(double angleDeg) = 0;
    virtual GalvoCommandResult getStepAngle(double& angleDeg) = 0;

    virtual GalvoCommandResult setAutoRotationAngle(int angleDeg) = 0;
    virtual GalvoCommandResult getAutoRotationAngle(int& angleDeg) = 0;

    virtual GalvoCommandResult setForwardSpeedMs(int speedMs) = 0;
    virtual GalvoCommandResult getForwardSpeedMs(int& speedMs) = 0;

    virtual GalvoCommandResult setReverseSpeedMs(int speedMs) = 0;
    virtual GalvoCommandResult getReverseSpeedMs(int& speedMs) = 0;

    virtual GalvoCommandResult setLaserDuty(int duty) = 0;
    virtual GalvoCommandResult setVoltageRange(double voltageV) = 0;
    virtual GalvoCommandResult getVoltageRange(double& voltageV) = 0;

    virtual GalvoCommandResult setScanDirection(GalvoScanDirection direction) = 0;
    virtual GalvoCommandResult laserOn() = 0;
    virtual GalvoCommandResult laserOff() = 0;
    virtual GalvoCommandResult startContinuousCapture() = 0;
};

using GalvoControllerPtr = std::unique_ptr<IGalvoController>;

/*
    函数功能：枚举当前系统可选的串口名称
    输入：
        无
    输出：
        返回值：按名称排序后的 COM 端口列表
*/
std::vector<std::string> enumerateSerialPortNames();

class SerialGalvoController final : public IGalvoController {
public:
    /*
        函数功能：构造串口振镜控制器
        输入：
            config：振镜串口与扫描参数配置
        输出：
            无（构造后保存配置，尚未真正打开串口）
    */
    explicit SerialGalvoController(GalvoScanConfig config);
    ~SerialGalvoController() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    GalvoCommandResult setSyncMode(GalvoSyncMode mode) override;
    GalvoCommandResult getSyncMode(GalvoSyncMode& mode) override;

    GalvoCommandResult setCaptureIntervalMs(int intervalMs) override;
    GalvoCommandResult getCaptureIntervalMs(int& intervalMs) override;

    GalvoCommandResult setContinuousCaptureWaitMs(int intervalMs) override;
    GalvoCommandResult getContinuousCaptureWaitMs(int& intervalMs) override;

    GalvoCommandResult setStepAngle(double angleDeg) override;
    GalvoCommandResult getStepAngle(double& angleDeg) override;

    GalvoCommandResult setAutoRotationAngle(int angleDeg) override;
    GalvoCommandResult getAutoRotationAngle(int& angleDeg) override;

    GalvoCommandResult setForwardSpeedMs(int speedMs) override;
    GalvoCommandResult getForwardSpeedMs(int& speedMs) override;

    GalvoCommandResult setReverseSpeedMs(int speedMs) override;
    GalvoCommandResult getReverseSpeedMs(int& speedMs) override;

    GalvoCommandResult setLaserDuty(int duty) override;
    GalvoCommandResult setVoltageRange(double voltageV) override;
    GalvoCommandResult getVoltageRange(double& voltageV) override;

    GalvoCommandResult setScanDirection(GalvoScanDirection direction) override;
    GalvoCommandResult laserOn() override;
    GalvoCommandResult laserOff() override;
    GalvoCommandResult startContinuousCapture() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace htmsr::app
