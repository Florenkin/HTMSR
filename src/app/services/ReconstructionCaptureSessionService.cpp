#include "app/services/ReconstructionCaptureSessionService.h"

#include "app/acquisition/GalvoController.h"
#include "app/acquisition/MockAcquisitionProvider.h"
#include "core/Logger.h"

#if HTMSR_WITH_HIK_CAMERA
#include "app/acquisition/HikCameraDevice.h"
#endif

#include <QDateTime>
#include <QDir>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace htmsr::app {
namespace {

constexpr int kLaserPreflightTestMs = 5000;
constexpr int kHardwareTriggerTimeoutMs = 3000;
constexpr int kMaxConsecutiveTriggerTimeouts = 3;

void ensureDirectory(const QString& directory)
{
    QDir dir(directory);
    if (dir.exists()) {
        return;
    }
    if (!dir.mkpath(".")) {
        throw std::runtime_error("Failed to create directory: " + directory.toStdString());
    }
}

AcquisitionSessionResult createSession(const std::string& outputDirectory)
{
    const QString root = QString::fromStdString(outputDirectory.empty() ? "." : outputDirectory);
    const QString captureRoot = QDir(root).filePath("reconstruction/capture");
    ensureDirectory(captureRoot);

    const QString sessionName = "reconstruction_capture_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString sessionDirectory = QDir(captureRoot).filePath(sessionName);
    ensureDirectory(sessionDirectory);
    ensureDirectory(QDir(sessionDirectory).filePath("left"));
    ensureDirectory(QDir(sessionDirectory).filePath("right"));

    AcquisitionSessionResult result;
    result.sessionDirectory = sessionDirectory.toStdString();
    result.leftDirectory = QDir(sessionDirectory).filePath("left").toStdString();
    result.rightDirectory = QDir(sessionDirectory).filePath("right").toStdString();
    return result;
}

std::string frameFileName(int frameIndex)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "frame_%06d.bmp", frameIndex + 1);
    return buffer;
}

CameraDeviceInfo findDeviceById(const std::vector<CameraDeviceInfo>& devices, const std::string& id)
{
    for (const auto& device : devices) {
        if (device.id == id) {
            return device;
        }
    }
    throw std::runtime_error("Camera device was not found, id=" + id);
}

void applyGalvoParameters(IGalvoController& controller, const GalvoScanConfig& config, bool applyDirectionCommand)
{
    const auto check = [](const GalvoCommandResult& result, const char* action) {
        if (!result.success) {
            throw std::runtime_error(std::string("Galvo command failed: ") + action + ". " + result.message);
        }
    };

    check(controller.setSyncMode(config.syncMode), "set sync mode");
    check(controller.setCaptureIntervalMs(config.captureIntervalMs), "set capture interval");
    check(controller.setContinuousCaptureWaitMs(config.continuousCaptureWaitMs), "set continuous capture wait");
    check(controller.setForwardSpeedMs(config.forwardSpeedMs), "set forward speed");
    check(controller.setReverseSpeedMs(config.reverseSpeedMs), "set reverse speed");
    check(controller.setLaserDuty(config.laserDuty), "set laser duty");
    check(controller.setVoltageRange(config.voltageRangeV), "set voltage range");
    if (applyDirectionCommand) {
        check(controller.setScanDirection(config.direction), "set scan direction");
    }
}

std::vector<unsigned char> laserSwitchCommand(bool enabled)
{
    return enabled
        ? std::vector<unsigned char>{ 0x55, 0xAA, 0x01, 0x1A, 0x1A }
        : std::vector<unsigned char>{ 0x55, 0xAA, 0x01, 0x1B, 0x1B };
}

bool saveFramePair(AcquisitionSessionResult& result, const FramePair& pair, int frameIndex)
{
    if (pair.left.empty() || pair.right.empty()) {
        ++result.failedFrameCount;
        Logger::instance().warning("ReconstructionCapture", "Captured empty reconstruction frame pair, frame=" + std::to_string(frameIndex + 1));
        return false;
    }

    const QString fileName = QString::fromStdString(frameFileName(frameIndex));
    const QString leftPath = QDir(QString::fromStdString(result.leftDirectory)).filePath(fileName);
    const QString rightPath = QDir(QString::fromStdString(result.rightDirectory)).filePath(fileName);
    if (!cv::imwrite(leftPath.toStdString(), pair.left) || !cv::imwrite(rightPath.toStdString(), pair.right)) {
        ++result.failedFrameCount;
        Logger::instance().error("ReconstructionCapture", "Failed to save reconstruction frame pair, frame=" + std::to_string(frameIndex + 1));
        return false;
    }

    result.leftImagePaths.push_back(leftPath.toStdString());
    result.rightImagePaths.push_back(rightPath.toStdString());
    result.lastLeftPreview = pair.left.clone();
    result.lastRightPreview = pair.right.clone();
    ++result.capturedFrameCount;
    return true;
}

void logCapturedFrameProgress(int frameIndex, int frameCount, const FramePair& pair, bool saved)
{
    const int frameNumber = frameIndex + 1;
    const bool shouldLog =
        frameNumber == 1 ||
        frameNumber == frameCount ||
        frameNumber % 10 == 0 ||
        !saved;

    if (!shouldLog) {
        return;
    }

    Logger::instance().info(
        "ReconstructionCapture",
        "Captured reconstruction frame " + std::to_string(frameNumber) + "/" + std::to_string(frameCount) +
            ", saved=" + std::string(saved ? "true" : "false") +
            ", left=" + std::to_string(pair.left.cols) + "x" + std::to_string(pair.left.rows) +
            ", right=" + std::to_string(pair.right.cols) + "x" + std::to_string(pair.right.rows));
}

} // namespace

AcquisitionSessionResult ReconstructionCaptureSessionService::capture(
    const IntegratedScanConfig& config,
    ProgressCallback progressCallback) const
{
    if (config.stereoCamera.frameCount <= 0) {
        throw std::runtime_error("Reconstruction capture frame count must be greater than zero.");
    }
    if (config.galvo.stepAngleDeg <= 0.0) {
        throw std::runtime_error("Galvo step angle must be greater than zero.");
    }

    AcquisitionSessionResult result = createSession(config.stereoCamera.outputDirectory);
    Logger::instance().info("ReconstructionCapture", "Reconstruction capture session created: " + result.sessionDirectory);

    const double requestedRotation = config.galvo.stepAngleDeg * static_cast<double>(config.stereoCamera.frameCount);
    if (std::abs(requestedRotation - config.totalRotationAngleDeg) > config.galvo.stepAngleDeg) {
        Logger::instance().warning(
            "ReconstructionCapture",
            "Frame count is used as the capture authority. Requested total rotation=" +
                std::to_string(config.totalRotationAngleDeg) +
                ", frameCount*step=" + std::to_string(requestedRotation));
    }

    if (config.stereoCamera.useMockProvider) {
        Logger::instance().warning("ReconstructionCapture", "Using mock acquisition provider. Galvo and laser commands will not be sent.");
        MockAcquisitionProvider provider(config.stereoCamera.frameCount);
        for (int frameIndex = 0; frameIndex < config.stereoCamera.frameCount && provider.hasNext(); ++frameIndex) {
            FramePair pair = provider.next();
            saveFramePair(result, pair, frameIndex);
            if (progressCallback) {
                progressCallback(frameIndex + 1, config.stereoCamera.frameCount, pair);
            }
        }
    } else {
#if HTMSR_WITH_HIK_CAMERA
        SerialGalvoController galvoController(config.galvo);
        if (!galvoController.connect()) {
            throw std::runtime_error(
                "Failed to open galvo serial port: " + config.galvo.portName +
                ". Check that the galvo controller is powered on, the COM port is correct, "
                "and no other program is using the port. Use mock acquisition to test without galvo hardware.");
        }

        try {
            GalvoScanConfig galvoConfig = config.galvo;
            const bool useHardwareTrigger = galvoConfig.syncMode == GalvoSyncMode::Sync;
            // 开始采集前的预检已使用“写入、重连、回读”确认步进角和自动旋转角。
            // 控制器在处理步进角设置期间可能丢弃紧随其后的命令，因此此处不能再快速重复写入这两个运动参数。
            // 软件同步只需进入异步时序后执行连续采集，不发送 0x04/0x05 方向命令以免切换到单次方向运动。
            applyGalvoParameters(galvoController, galvoConfig, useHardwareTrigger);

            if (useHardwareTrigger) {
                const double exposureUs = std::max(config.stereoCamera.leftParameters.exposureTime, config.stereoCamera.rightParameters.exposureTime);
                const double triggerIntervalUs = static_cast<double>(std::max(1, galvoConfig.captureIntervalMs)) * 1000.0;
                if (exposureUs > triggerIntervalUs) {
                    Logger::instance().warning(
                        "ReconstructionCapture",
                        "Hardware trigger timing may drop frames: exposure=" + std::to_string(exposureUs) +
                            "us is longer than galvo trigger interval=" + std::to_string(triggerIntervalUs) +
                            "us. Lower exposure or increase the capture interval.");
                }
            }

            const auto laserPreflightResult = galvoController.sendRawCommand(laserSwitchCommand(true), false);
            if (!laserPreflightResult.success) {
                throw std::runtime_error("Failed to turn laser on for reconstruction preflight test.");
            }
            Logger::instance().info(
                "ReconstructionCapture",
                "Laser preflight test started. Laser will stay on for 5 seconds before reconstruction capture.");
            std::this_thread::sleep_for(std::chrono::milliseconds(kLaserPreflightTestMs));
            Logger::instance().info("ReconstructionCapture", "Laser preflight test finished. Starting reconstruction capture.");

            StereoCameraConfig cameraConfig = config.stereoCamera;
            cameraConfig.leftParameters.useHardwareTrigger = useHardwareTrigger;
            cameraConfig.rightParameters.useHardwareTrigger = useHardwareTrigger;
            if (useHardwareTrigger) {
                cameraConfig.leftParameters.grabTimeoutMs = std::max(cameraConfig.leftParameters.grabTimeoutMs, kHardwareTriggerTimeoutMs);
                cameraConfig.rightParameters.grabTimeoutMs = std::max(cameraConfig.rightParameters.grabTimeoutMs, kHardwareTriggerTimeoutMs);
            }

            const auto devices = enumerateHikCameraDevices();
            HikCameraDevice leftCamera(findDeviceById(devices, cameraConfig.leftDeviceId));
            HikCameraDevice rightCamera(findDeviceById(devices, cameraConfig.rightDeviceId));

            if (!leftCamera.connect() || !rightCamera.connect()) {
                throw std::runtime_error("Failed to connect Hik stereo cameras.");
            }
            if (!leftCamera.configure(cameraConfig.leftParameters)) {
                throw std::runtime_error("Failed to configure left Hik camera: " + leftCamera.lastError());
            }
            if (!rightCamera.configure(cameraConfig.rightParameters)) {
                throw std::runtime_error("Failed to configure right Hik camera: " + rightCamera.lastError());
            }
            if (!leftCamera.startGrabbing() || !rightCamera.startGrabbing()) {
                throw std::runtime_error("Failed to start Hik stereo grabbing.");
            }

            if (!useHardwareTrigger) {
                // 连续扫描开始前清掉实时预览遗留帧；后续保存的第一组图对应本次扫描。
                for (int i = 0; i < 3; ++i) {
                    leftCamera.grabFrame(100);
                    rightCamera.grabFrame(100);
                }
            }

            const auto laserOnResult = galvoController.sendRawCommand(laserSwitchCommand(true), false);
            if (!laserOnResult.success) {
                throw std::runtime_error("Failed to keep laser on for reconstruction capture.");
            }
            Logger::instance().info("ReconstructionCapture", "Laser switched on for reconstruction capture.");
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(200, config.galvo.continuousCaptureWaitMs)));

            // 无论相机由外部脉冲还是自由取流，本次重建都必须让振镜以步进角、总旋转角和时间间隔连续扫描。
            // 软件同步模式仅改变相机的取帧方式，不能把“方向”命令当作每帧的单步运动命令。
            const auto continuousCaptureResult = galvoController.startContinuousCapture();
            if (!continuousCaptureResult.success) {
                throw std::runtime_error(
                    continuousCaptureResult.message.empty()
                        ? "Failed to start galvo continuous reconstruction scan."
                        : continuousCaptureResult.message);
            }
            Logger::instance().info(
                "ReconstructionCapture",
                useHardwareTrigger
                    ? "Galvo synchronized scan started for reconstruction capture. Cameras will wait for galvo hardware triggers."
                    : "Software-sync continuous scan command sent. Galvo should rotate continuously while cameras save free-run frame pairs.");

            int consecutiveTriggerTimeouts = 0;
            auto nextSoftwareCapture = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(std::max(1, galvoConfig.continuousCaptureWaitMs));
            for (int frameIndex = 0; frameIndex < config.stereoCamera.frameCount; ++frameIndex) {
                if (!useHardwareTrigger) {
                    std::this_thread::sleep_until(nextSoftwareCapture);
                    nextSoftwareCapture += std::chrono::milliseconds(std::max(1, galvoConfig.captureIntervalMs));
                }

                FramePair pair;
                pair.frameIndex = frameIndex;
                pair.left = leftCamera.grabFrame(cameraConfig.leftParameters.grabTimeoutMs);
                pair.right = rightCamera.grabFrame(cameraConfig.rightParameters.grabTimeoutMs);
                const bool saved = saveFramePair(result, pair, frameIndex);
                logCapturedFrameProgress(frameIndex, config.stereoCamera.frameCount, pair, saved);

                if (saved) {
                    consecutiveTriggerTimeouts = 0;
                } else if (pair.left.empty() || pair.right.empty()) {
                    ++consecutiveTriggerTimeouts;
                    if (consecutiveTriggerTimeouts >= kMaxConsecutiveTriggerTimeouts) {
                        result.message = useHardwareTrigger
                            ? "Reconstruction capture stopped because cameras did not receive hardware trigger frames. "
                              "Check trigger line " + std::to_string(cameraConfig.leftParameters.triggerSourceLine) +
                              ", camera trigger polarity, galvo trigger wiring, and galvo sync output. "
                              "No stereo images were saved because both cameras must provide a frame for each pair."
                            : "Reconstruction capture stopped because cameras did not provide valid live frames. "
                              "Check camera connection, exposure, and camera streaming state.";
                        Logger::instance().error("ReconstructionCapture", result.message);
                        break;
                    }
                }

                if (progressCallback) {
                    progressCallback(frameIndex + 1, config.stereoCamera.frameCount, pair);
                }

            }

            leftCamera.stopGrabbing();
            rightCamera.stopGrabbing();
            leftCamera.disconnect();
            rightCamera.disconnect();
            galvoController.sendRawCommand(laserSwitchCommand(false), false);
            galvoController.disconnect();
        } catch (...) {
            galvoController.sendRawCommand(laserSwitchCommand(false), false);
            galvoController.disconnect();
            throw;
        }
#else
        throw std::runtime_error("Hik camera support is disabled. Enable HTMSR_ENABLE_HIK_CAMERA or use mock provider.");
#endif
    }

    if (result.message.empty()) {
        result.success = result.capturedFrameCount > 0;
        result.message = result.success
            ? "Reconstruction capture finished, frames=" + std::to_string(result.capturedFrameCount)
            : "Reconstruction capture finished without valid frames.";
    }
    Logger::instance().info("ReconstructionCapture", result.message + ", directory=" + result.sessionDirectory);
    return result;
}

} // namespace htmsr::app
