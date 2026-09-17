#include "app/acquisition/GalvoCaptureSupport.h"

#include "core/Logger.h"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>

namespace htmsr::app {
namespace {

GalvoWait makeWait(GalvoWait wait)
{
    return wait ? std::move(wait) : GalvoWait{[](int milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }};
}

void requireCommand(const GalvoCommandResult& result, const char* action)
{
    if (!result.success) {
        throw std::runtime_error(std::string("Galvo command failed: ") + action + ". " + result.message);
    }
}

std::string connectionFailure(const IGalvoController& controller, const std::string& fallback)
{
    const auto detail = controller.lastError();
    return detail.empty() ? fallback : detail;
}

} // namespace

VerifiedGalvoMotionParameters configureAndVerifyGalvoMotion(
    IGalvoController& controller, const GalvoScanConfig& config, GalvoWait wait)
{
    wait = makeWait(std::move(wait));
    if (!std::isfinite(config.stepAngleDeg) || config.stepAngleDeg <= 0.0 || config.autoRotationAngleDeg <= 0) {
        throw std::runtime_error("Invalid galvo rotation parameters.");
    }
    if (!controller.isConnected() && !controller.connect()) {
        throw std::runtime_error(connectionFailure(controller, "Failed to connect galvo serial port: " + config.portName));
    }
    const auto reconnect = [&]() {
        controller.disconnect();
        wait(80);
        if (!controller.connect()) {
            return false;
        }
        wait(50);
        return true;
    };
    const auto queryWithRecovery = [&](const auto& query) {
        GalvoCommandResult result;
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (attempt > 0 && !reconnect()) {
                result.success = false;
                result.message = connectionFailure(controller, "Failed to reconnect galvo for parameter readback.");
            } else {
                result = query();
                if (result.success) {
                    return result;
                }
            }
            if (attempt < 3) {
                wait(120);
            }
        }
        return result;
    };
    std::string lastFailure;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0 && !reconnect()) {
            lastFailure = connectionFailure(controller, "Failed to reconnect galvo before setting rotation angles.");
            continue;
        }
        const auto stepSet = controller.setStepAngle(config.stepAngleDeg);
        if (!stepSet.success) {
            lastFailure = "Failed to set step angle: " + stepSet.message;
            continue;
        }
        wait(80);
        if (!reconnect()) {
            lastFailure = connectionFailure(controller, "Failed to reconnect galvo between angle settings.");
            continue;
        }
        const auto totalSet = controller.setAutoRotationAngle(config.autoRotationAngleDeg);
        if (!totalSet.success) {
            lastFailure = "Failed to set total rotation angle: " + totalSet.message;
            continue;
        }
        wait(300);
        double actualStep = 0.0;
        int actualTotal = 0;
        const auto stepRead = queryWithRecovery([&]() { return controller.getStepAngle(actualStep); });
        if (!stepRead.success) {
            lastFailure = "Step angle readback failed: " + stepRead.message;
            continue;
        }
        const auto totalRead = queryWithRecovery([&]() { return controller.getAutoRotationAngle(actualTotal); });
        if (!totalRead.success) {
            lastFailure = "Total angle readback failed: " + totalRead.message;
            continue;
        }
        if (!std::isfinite(actualStep) || actualStep <= 0.0 || actualTotal <= 0 ||
            std::abs(actualStep - config.stepAngleDeg) > 0.006 || actualTotal != config.autoRotationAngleDeg) {
            lastFailure = "Galvo angle readback differs from requested settings: step=" + std::to_string(actualStep) +
                ", total=" + std::to_string(actualTotal);
            continue;
        }
        return {config.portName, actualStep, actualTotal};
    }
    throw std::runtime_error(lastFailure);
}

VerifiedGalvoMotionParameters prepareGalvoForCapture(
    IGalvoController& controller, const IntegratedScanConfig& config, GalvoWait wait)
{
    wait = makeWait(std::move(wait));
    VerifiedGalvoMotionParameters motion;
    if (config.verifiedGalvoMotion) {
        motion = *config.verifiedGalvoMotion;
        if (motion.portName != config.galvo.portName || !std::isfinite(motion.stepAngleDeg) ||
            motion.stepAngleDeg <= 0.0 || motion.totalRotationAngleDeg <= 0 ||
            std::abs(motion.stepAngleDeg - config.galvo.stepAngleDeg) > 0.0001 ||
            motion.totalRotationAngleDeg != config.galvo.autoRotationAngleDeg ||
            std::abs(motion.totalRotationAngleDeg - config.totalRotationAngleDeg) > 0.0001 ||
            frameCountForGalvoScan(motion.totalRotationAngleDeg, motion.stepAngleDeg) != config.stereoCamera.frameCount) {
            throw std::runtime_error("Verified galvo preflight does not match this capture's port, angles or frame count.");
        }
        Logger::instance().info("ReconstructionCapture", "Reusing this capture's verified angle readback; duplicate angle queries will not be sent.");
    } else {
        motion = configureAndVerifyGalvoMotion(controller, config.galvo, wait);
    }
    if (!controller.isConnected() && !controller.connect()) {
        throw std::runtime_error(connectionFailure(controller, "Failed to connect galvo serial port for scan parameters."));
    }
    // 写入成功只代表串口已发送。参数之间留出固件处理时间，不能把查询无返回等同于写入失败。
    const auto send = [&](const GalvoCommandResult& result, const char* action) {
        requireCommand(result, action);
        wait(150);
    };
    send(controller.setSyncMode(config.galvo.syncMode), "set sync mode");
    send(controller.setCaptureIntervalMs(config.galvo.captureIntervalMs), "set capture interval");
    send(controller.setContinuousCaptureWaitMs(config.galvo.continuousCaptureWaitMs), "set continuous capture wait");
    send(controller.setForwardSpeedMs(config.galvo.forwardSpeedMs), "set forward speed");
    send(controller.setReverseSpeedMs(config.galvo.reverseSpeedMs), "set reverse speed");
    send(controller.setLaserDuty(config.galvo.laserDuty), "set laser duty");
    send(controller.setVoltageRange(config.galvo.voltageRangeV), "set voltage range");
    Logger::instance().info("ReconstructionCapture", "Scan parameters sent with firmware settling intervals. "
        "Sync query is not a mandatory capture gate; hardware frame continuity and counts remain mandatory.");
    return motion;
}

GalvoLaserCaptureGuard::GalvoLaserCaptureGuard(IGalvoController& controller, GalvoWait wait)
    : controller_(controller), wait_(makeWait(std::move(wait)))
{
}

GalvoLaserCaptureGuard::~GalvoLaserCaptureGuard()
{
    if (closeRequired_) {
        close();
    }
}

void GalvoLaserCaptureGuard::turnOn()
{
    if (closeRequired_) {
        throw std::runtime_error("Laser turn-on was already requested for this capture.");
    }
    // 即使串口报告部分写入失败，也可能已经点亮激光，所以异常清理仍须发送关闭。
    closeRequired_ = true;
    requireCommand(controller_.laserOn(), "turn laser on");
    Logger::instance().info("ReconstructionCapture", "Laser ON command sent once for capture: 55 AA 01 1A 1A.");
}

GalvoCommandResult GalvoLaserCaptureGuard::close() noexcept
{
    GalvoCommandResult result;
    result.success = true;
    if (!closeRequired_) {
        return result;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            result = controller_.laserOff();
            if (result.success) {
                closeRequired_ = false;
                Logger::instance().info("ReconstructionCapture", "Laser OFF command sent: 55 AA 01 1B 1B.");
                return result;
            }
            if (attempt < 2) {
                wait_(100);
            }
        } catch (...) {
            result.success = false;
            result.message = "Laser OFF command threw an exception.";
        }
    }
    try {
        Logger::instance().error("ReconstructionCapture", "LASER OFF FAILED; physical laser state is unconfirmed. " + result.message);
    } catch (...) {
    }
    return result;
}

} // namespace htmsr::app
