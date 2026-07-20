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

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace htmsr::app {
namespace {

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
    ensureDirectory(root);

    const QString sessionName = "reconstruction_capture_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString sessionDirectory = QDir(root).filePath(sessionName);
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

void applyGalvoParameters(IGalvoController& controller, const GalvoScanConfig& config)
{
    const auto check = [](const GalvoCommandResult& result, const char* action) {
        if (!result.success) {
            throw std::runtime_error(std::string("Galvo command failed: ") + action + ". " + result.message);
        }
    };

    check(controller.setSyncMode(config.syncMode), "set sync mode");
    check(controller.setCaptureIntervalMs(config.captureIntervalMs), "set capture interval");
    check(controller.setContinuousCaptureWaitMs(config.continuousCaptureWaitMs), "set continuous capture wait");
    check(controller.setStepAngle(config.stepAngleDeg), "set step angle");
    check(controller.setAutoRotationAngle(config.autoRotationAngleDeg), "set auto rotation angle");
    check(controller.setForwardSpeedMs(config.forwardSpeedMs), "set forward speed");
    check(controller.setReverseSpeedMs(config.reverseSpeedMs), "set reverse speed");
    check(controller.setLaserDuty(config.laserDuty), "set laser duty");
    check(controller.setVoltageRange(config.voltageRangeV), "set voltage range");
    check(controller.setScanDirection(config.direction), "set scan direction");
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
        MockAcquisitionProvider provider(config.stereoCamera.frameCount);
        for (int frameIndex = 0; frameIndex < config.stereoCamera.frameCount && provider.hasNext(); ++frameIndex) {
            FramePair pair = provider.next();
            saveFramePair(result, pair, frameIndex);
            if (progressCallback) {
                progressCallback(frameIndex + 1, config.stereoCamera.frameCount);
            }
        }
    } else {
#if HTMSR_WITH_HIK_CAMERA
        SerialGalvoController galvoController(config.galvo);
        if (!galvoController.connect()) {
            throw std::runtime_error("Failed to connect galvo controller.");
        }

        try {
            applyGalvoParameters(galvoController, config.galvo);

            const auto devices = enumerateHikCameraDevices();
            HikCameraDevice leftCamera(findDeviceById(devices, config.stereoCamera.leftDeviceId));
            HikCameraDevice rightCamera(findDeviceById(devices, config.stereoCamera.rightDeviceId));

            if (!leftCamera.connect() || !rightCamera.connect()) {
                throw std::runtime_error("Failed to connect Hik stereo cameras.");
            }
            if (!leftCamera.configure(config.stereoCamera.leftParameters) || !rightCamera.configure(config.stereoCamera.rightParameters)) {
                throw std::runtime_error("Failed to configure Hik stereo cameras.");
            }
            if (!leftCamera.startGrabbing() || !rightCamera.startGrabbing()) {
                throw std::runtime_error("Failed to start Hik stereo grabbing.");
            }

            for (int frameIndex = 0; frameIndex < config.stereoCamera.frameCount; ++frameIndex) {
                const auto laserOnResult = galvoController.laserOn();
                if (!laserOnResult.success) {
                    throw std::runtime_error("Failed to turn laser on.");
                }

                FramePair pair;
                pair.frameIndex = frameIndex;
                pair.left = leftCamera.grabFrame(config.stereoCamera.leftParameters.grabTimeoutMs);
                pair.right = rightCamera.grabFrame(config.stereoCamera.rightParameters.grabTimeoutMs);
                galvoController.laserOff();
                saveFramePair(result, pair, frameIndex);

                if (frameIndex + 1 < config.stereoCamera.frameCount) {
                    const auto stepResult = galvoController.setScanDirection(config.galvo.direction);
                    if (!stepResult.success) {
                        throw std::runtime_error("Failed to rotate galvo by one step.");
                    }
                }

                if (progressCallback) {
                    progressCallback(frameIndex + 1, config.stereoCamera.frameCount);
                }
            }

            leftCamera.stopGrabbing();
            rightCamera.stopGrabbing();
            leftCamera.disconnect();
            rightCamera.disconnect();
            galvoController.laserOff();
            galvoController.disconnect();
        } catch (...) {
            galvoController.laserOff();
            galvoController.disconnect();
            throw;
        }
#else
        throw std::runtime_error("Hik camera support is disabled. Enable HTMSR_ENABLE_HIK_CAMERA or use mock provider.");
#endif
    }

    result.success = result.capturedFrameCount > 0;
    result.message = result.success
        ? "Reconstruction capture finished, frames=" + std::to_string(result.capturedFrameCount)
        : "Reconstruction capture finished without valid frames.";
    Logger::instance().info("ReconstructionCapture", result.message + ", directory=" + result.sessionDirectory);
    return result;
}

} // namespace htmsr::app
