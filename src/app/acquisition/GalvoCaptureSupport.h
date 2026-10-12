#pragma once

#include "app/acquisition/GalvoController.h"

#include <functional>

namespace htmsr::app {

using GalvoWait = std::function<void(int milliseconds)>;

// 角度写入、重连及回读只执行一次；允许按设备响应特性重试，失败仍然中止。
VerifiedGalvoMotionParameters configureAndVerifyGalvoMotion(
    IGalvoController& controller, const GalvoScanConfig& config, GalvoWait wait = {});

// 复用本次预检结果，不在批量写入之后再次强制查询；无预检结果的入口先执行相同角度预检。
VerifiedGalvoMotionParameters prepareGalvoForCapture(
    IGalvoController& controller, const IntegratedScanConfig& config, GalvoWait wait = {});

// 持有本次采集的激光开关责任。成功、超时、写盘异常、启动失败均在串口释放前尝试关闭。
class GalvoLaserCaptureGuard {
public:
    explicit GalvoLaserCaptureGuard(IGalvoController& controller, GalvoWait wait = {});
    ~GalvoLaserCaptureGuard();
    GalvoLaserCaptureGuard(const GalvoLaserCaptureGuard&) = delete;
    GalvoLaserCaptureGuard& operator=(const GalvoLaserCaptureGuard&) = delete;

    void turnOn();
    GalvoCommandResult close() noexcept;

private:
    IGalvoController& controller_;
    GalvoWait wait_;
    bool closeRequired_ = false;
};

} // namespace htmsr::app
