#include "app/services/IntegratedScanService.h"

#include "app/acquisition/GalvoController.h"
#if HTMSR_WITH_HIK_CAMERA
#include "app/acquisition/HikCameraDevice.h"
#endif
#include "core/CalibrationService.h"
#include "core/Logger.h"
#include "core/ReconstructionService.h"

#include <QDateTime>
#include <QDir>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace htmsr::app {
namespace {

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

AcquisitionSessionResult createSession(const StereoCameraConfig& config)
{
    AcquisitionSessionResult result;
    const QString root = QString::fromStdString(config.outputDirectory.empty() ? "." : config.outputDirectory);
    const QString captureRoot = QDir(root).filePath("reconstruction/capture");
    ensureDirectory(captureRoot);

    const QString sessionName = "scan_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString sessionDirectory = QDir(captureRoot).filePath(sessionName);
    ensureDirectory(sessionDirectory);
    ensureDirectory(QDir(sessionDirectory).filePath("left"));
    ensureDirectory(QDir(sessionDirectory).filePath("right"));

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

StereoCameraConfig makeScanCameraConfig(const IntegratedScanConfig& config)
{
    StereoCameraConfig cameraConfig = config.stereoCamera;
    const bool useHardwareTrigger = cameraConfig.leftParameters.useHardwareTrigger ||
        cameraConfig.rightParameters.useHardwareTrigger;
    cameraConfig.leftParameters.useHardwareTrigger = useHardwareTrigger;
    cameraConfig.rightParameters.useHardwareTrigger = useHardwareTrigger;
    if (useHardwareTrigger) {
        cameraConfig.leftParameters.grabTimeoutMs = std::max(cameraConfig.leftParameters.grabTimeoutMs, kHardwareTriggerTimeoutMs);
        cameraConfig.rightParameters.grabTimeoutMs = std::max(cameraConfig.rightParameters.grabTimeoutMs, kHardwareTriggerTimeoutMs);
    }
    return cameraConfig;
}

} // namespace

/*
    函数功能：执行振镜与海康双相机联动扫描，并在采集完成后自动重建点云
    输入：
        config：一键扫描重建所需的振镜、相机、标定与重建配置
    输出：
        返回值：包含采集结果、标定复用情况、重建结果和提示信息的工作流结果
*/
IntegratedWorkflowResult IntegratedScanService::runScanAndReconstruct(const IntegratedScanConfig& config, ProgressCallback progressCallback) const
{
#if !HTMSR_WITH_HIK_CAMERA
    (void)config;
    throw std::runtime_error("Integrated scan requires Hik camera support. Enable HTMSR_WITH_HIK_CAMERA first.");
#else
    if (config.forceRecalibration) {
        IntegratedWorkflowResult result;
        result.success = false;
        result.message = "Force recalibration is enabled. Run auto calibration workflow first.";
        Logger::instance().warning("IntegratedWorkflow", result.message);
        return result;
    }
    if (config.stereoCamera.useMockProvider) {
        throw std::runtime_error("Integrated scan and reconstruct requires real cameras. Disable mock provider first.");
    }

    CalibrationService calibrationService;
    ReconstructionService reconstructionService;
    IntegratedWorkflowResult result;

    Logger::instance().info("IntegratedWorkflow", "Starting integrated scan and reconstruction workflow.");
    if (!calibrationService.loadCalibration(config.calibrationFile, result.calibration) || !result.calibration.isValid()) {
        result.success = false;
        result.message = "No valid calibration file was found. Run auto calibration workflow first.";
        Logger::instance().warning("IntegratedWorkflow", result.message);
        return result;
    }
    result.usedExistingCalibration = true;

    SerialGalvoController galvoController(config.galvo);
    if (!galvoController.connect()) {
        throw std::runtime_error(
            "Failed to open galvo serial port: " + config.galvo.portName +
            ". Check that the galvo controller is powered on, the COM port is correct, "
            "and no other program is using the port.");
    }
    const bool useHardwareTrigger = config.stereoCamera.leftParameters.useHardwareTrigger ||
        config.stereoCamera.rightParameters.useHardwareTrigger;
    GalvoScanConfig deviceGalvoConfig = config.galvo;
    applyGalvoParameters(galvoController, deviceGalvoConfig, useHardwareTrigger);

    const StereoCameraConfig cameraConfig = makeScanCameraConfig(config);
    const auto devices = enumerateHikCameraDevices();
    HikCameraDevice leftCamera(findDeviceById(devices, cameraConfig.leftDeviceId));
    HikCameraDevice rightCamera(findDeviceById(devices, cameraConfig.rightDeviceId));

    if (!leftCamera.connect() || !rightCamera.connect()) {
        throw std::runtime_error("Failed to connect Hik stereo cameras.");
    }
    if (!leftCamera.configure(cameraConfig.leftParameters) || !rightCamera.configure(cameraConfig.rightParameters)) {
        throw std::runtime_error("Failed to configure Hik stereo cameras.");
    }
    if (!leftCamera.startGrabbing() || !rightCamera.startGrabbing()) {
        throw std::runtime_error("Failed to start Hik stereo grabbing.");
    }

    result.acquisition = createSession(cameraConfig);
    Logger::instance().info("IntegratedWorkflow", "Scan session created: " + result.acquisition.sessionDirectory);

    const auto laserOnResult = galvoController.sendRawCommand(laserSwitchCommand(true), false);
    if (!laserOnResult.success) {
        throw std::runtime_error("Failed to turn laser on.");
    }
    Logger::instance().info("IntegratedWorkflow", "Laser switched on for integrated scan.");
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(0, config.galvo.continuousCaptureWaitMs)));

    const bool cameraUsesHardwareTrigger = cameraConfig.leftParameters.useHardwareTrigger || cameraConfig.rightParameters.useHardwareTrigger;
    try {
        if (cameraUsesHardwareTrigger) {
            const auto captureResult = galvoController.startContinuousCapture();
            if (!captureResult.success) {
                throw std::runtime_error("Failed to trigger galvo continuous capture.");
            }
        }
        Logger::instance().info(
            "IntegratedWorkflow",
            cameraUsesHardwareTrigger
                ? "Galvo continuous capture started for integrated scan."
                : "Software-sync integrated scan started. Galvo will move one configured step between saved frame pairs.");

        if (!cameraUsesHardwareTrigger) {
            for (int i = 0; i < 3; ++i) {
                leftCamera.grabFrame(100);
                rightCamera.grabFrame(100);
            }
        }
        int consecutiveTriggerTimeouts = 0;
        for (int frameIndex = 0; frameIndex < cameraConfig.frameCount; ++frameIndex) {
            FramePair pair;
            pair.frameIndex = frameIndex;
            pair.left = leftCamera.grabFrame(cameraConfig.leftParameters.grabTimeoutMs);
            pair.right = rightCamera.grabFrame(cameraConfig.rightParameters.grabTimeoutMs);
            if (progressCallback) {
                progressCallback(frameIndex + 1, cameraConfig.frameCount, pair);
            }

            if (pair.left.empty() || pair.right.empty()) {
                ++result.acquisition.failedFrameCount;
                Logger::instance().warning("IntegratedWorkflow", "Captured empty scan frame pair, frame=" + std::to_string(frameIndex + 1));
                ++consecutiveTriggerTimeouts;
                if (consecutiveTriggerTimeouts >= kMaxConsecutiveTriggerTimeouts) {
                    result.acquisition.message = cameraUsesHardwareTrigger
                        ? "Integrated scan stopped because cameras did not receive hardware trigger frames. "
                          "Check the camera trigger line setting, camera trigger polarity, galvo trigger wiring, and galvo sync output."
                        : "Integrated scan stopped because cameras did not provide valid live frames. "
                          "Check camera connection, exposure, and camera streaming state.";
                    Logger::instance().error("IntegratedWorkflow", result.acquisition.message);
                    break;
                }
                continue;
            }
            consecutiveTriggerTimeouts = 0;

            const QString fileName = QString::fromStdString(frameFileName(frameIndex));
            const QString leftPath = QDir(QString::fromStdString(result.acquisition.leftDirectory)).filePath(fileName);
            const QString rightPath = QDir(QString::fromStdString(result.acquisition.rightDirectory)).filePath(fileName);
            if (!cv::imwrite(leftPath.toStdString(), pair.left) || !cv::imwrite(rightPath.toStdString(), pair.right)) {
                ++result.acquisition.failedFrameCount;
                Logger::instance().error("IntegratedWorkflow", "Failed to save scan frame pair, frame=" + std::to_string(frameIndex + 1));
                continue;
            }

            result.acquisition.leftImagePaths.push_back(leftPath.toStdString());
            result.acquisition.rightImagePaths.push_back(rightPath.toStdString());
            result.acquisition.lastLeftPreview = pair.left.clone();
            result.acquisition.lastRightPreview = pair.right.clone();
            ++result.acquisition.capturedFrameCount;

            if (!cameraUsesHardwareTrigger && frameIndex + 1 < cameraConfig.frameCount) {
                const auto stepResult = galvoController.setScanDirection(config.galvo.direction);
                if (!stepResult.success) {
                    throw std::runtime_error("Failed to move galvo one software-sync step. " + stepResult.message);
                }
                const int settleMs = std::max({ 1, config.galvo.captureIntervalMs, config.galvo.forwardSpeedMs, config.galvo.reverseSpeedMs });
                std::this_thread::sleep_for(std::chrono::milliseconds(settleMs));
            }
        }
    } catch (...) {
        galvoController.sendRawCommand(laserSwitchCommand(false), false);
        throw;
    }

    galvoController.sendRawCommand(laserSwitchCommand(false), false);
    leftCamera.stopGrabbing();
    rightCamera.stopGrabbing();
    leftCamera.disconnect();
    rightCamera.disconnect();
    galvoController.disconnect();

    if (!result.acquisition.message.empty()) {
        result.acquisition.success = false;
    } else {
        result.acquisition.success = result.acquisition.capturedFrameCount > 0;
        result.acquisition.message = result.acquisition.success
            ? "Integrated scan finished, frames=" + std::to_string(result.acquisition.capturedFrameCount)
            : "Integrated scan finished without valid frames.";
    }

    if (!result.acquisition.success) {
        result.success = false;
        result.message = result.acquisition.message;
        Logger::instance().warning("IntegratedWorkflow", result.message);
        return result;
    }

    ReconstructionInput reconstructionInput;
    reconstructionInput.leftDirectory = result.acquisition.leftDirectory;
    reconstructionInput.rightDirectory = result.acquisition.rightDirectory;
    reconstructionInput.imageRange = config.reconstructionRange;
    reconstructionInput.calibration = result.calibration;
    reconstructionInput.laserConfig = config.laserConfig;
    reconstructionInput.matchDistanceThreshold = config.matchDistanceThreshold;

    result.reconstruction = reconstructionService.reconstruct(reconstructionInput);
    result.success = result.reconstruction.success;
    result.message = result.success
        ? "Integrated scan and reconstruction finished. Points=" + std::to_string(result.reconstruction.mergedPoints.size())
        : result.reconstruction.message;
    if (result.success) {
        Logger::instance().info("IntegratedWorkflow", result.message);
    } else {
        Logger::instance().warning("IntegratedWorkflow", result.message);
    }
    return result;
#endif
}

} // namespace htmsr::app
