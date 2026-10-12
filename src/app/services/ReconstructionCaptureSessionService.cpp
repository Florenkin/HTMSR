#include "app/services/ReconstructionCaptureSessionService.h"

#include "app/acquisition/GalvoController.h"
#include "app/acquisition/GalvoCaptureSupport.h"
#include "app/acquisition/MockAcquisitionProvider.h"
#include "app/services/ReconstructionStorage.h"
#include "core/Logger.h"

#if HTMSR_WITH_HIK_CAMERA
#include "app/acquisition/HikCameraDevice.h"
#endif

#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace htmsr::app {
namespace {

constexpr int kHardwareTriggerTimeoutMs = 3000;
constexpr int kMaxConsecutiveLiveTimeouts = 3;

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
    const QString sessionDirectory = ReconstructionStorage::createCaptureDirectory(outputDirectory);
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
    bool leftSaved = false;
    bool rightSaved = false;
    try {
        leftSaved = cv::imwrite(leftPath.toStdString(), pair.left);
        rightSaved = cv::imwrite(rightPath.toStdString(), pair.right);
    } catch (const cv::Exception& ex) {
        Logger::instance().error("ReconstructionCapture", "Image save exception: " + std::string(ex.what()));
    }
    if (!leftSaved || !rightSaved) {
        // 本次会话的新文件；左右保存任一失败时撤回这组不完整文件，防止目录排序错配。
        QFile::remove(leftPath);
        QFile::remove(rightPath);
        ++result.failedFrameCount;
        Logger::instance().error("ReconstructionCapture", "Failed to save reconstruction frame pair, frame=" + std::to_string(frameIndex + 1));
        return false;
    }

    result.leftImagePaths.push_back(leftPath.toStdString());
    result.rightImagePaths.push_back(rightPath.toStdString());
    result.lastLeftPreview = pair.left;
    result.lastRightPreview = pair.right;
    ++result.capturedFrameCount;
    return true;
}

void logCapturedFrameProgress(int frameIndex, int frameCount, const FramePair& pair, bool saved)
{
    const int frameNumber = frameIndex + 1;
    const int progressInterval = std::max(1, frameCount / 10 + (frameCount % 10 != 0));
    const bool shouldLog =
        frameNumber == 1 ||
        frameNumber == frameCount ||
        frameNumber % progressInterval == 0 ||
        !saved;

    if (!shouldLog) {
        return;
    }

    Logger::instance().info(
        "ReconstructionCapture",
        "采集进度：" + std::to_string(frameNumber) + "/" + std::to_string(frameCount) +
            (saved ? " 组已保存" : " 组，当前帧保存失败"));
    Logger::instance().debug("ReconstructionCapture",
        "Frame " + std::to_string(frameNumber) + ", saved=" + std::string(saved ? "true" : "false") +
            ", left=" + std::to_string(pair.left.cols) + "x" + std::to_string(pair.left.rows) +
            ", right=" + std::to_string(pair.right.cols) + "x" + std::to_string(pair.right.rows));
}

} // namespace

AcquisitionSessionResult ReconstructionCaptureSessionService::capture(
    const IntegratedScanConfig& inputConfig,
    ProgressCallback progressCallback, CancellationToken cancellation) const
{
    cancellation.check();
    IntegratedScanConfig config = inputConfig;
    if (config.stereoCamera.frameCount <= 0) {
        throw std::runtime_error("Reconstruction capture frame count must be greater than zero.");
    }
    if (config.galvo.stepAngleDeg <= 0.0) {
        throw std::runtime_error("Galvo step angle must be greater than zero.");
    }
    const bool useHardwareTrigger = config.stereoCamera.leftParameters.useHardwareTrigger ||
        config.stereoCamera.rightParameters.useHardwareTrigger;
    if (!config.stereoCamera.useMockProvider && useHardwareTrigger &&
        (config.galvo.syncMode != GalvoSyncMode::Sync ||
         !config.stereoCamera.leftParameters.useHardwareTrigger || !config.stereoCamera.rightParameters.useHardwareTrigger ||
         config.stereoCamera.leftParameters.triggerSourceLine != config.stereoCamera.rightParameters.triggerSourceLine ||
         config.stereoCamera.leftDeviceId == config.stereoCamera.rightDeviceId)) {
        throw std::runtime_error("Hardware stereo capture requires galvo Sync mode, two different cameras, and the same trigger input on both cameras.");
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
            cancellation.check();
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
            throw std::runtime_error(galvoController.lastError());
        }

        try {
            GalvoLaserCaptureGuard laserGuard(galvoController);
            const auto motion = prepareGalvoForCapture(galvoController, config, [cancellation](int ms) { cancellation.wait(ms); });
            config.galvo.stepAngleDeg = motion.stepAngleDeg;
            config.galvo.autoRotationAngleDeg = motion.totalRotationAngleDeg;
            config.totalRotationAngleDeg = motion.totalRotationAngleDeg;
            config.stereoCamera.frameCount = frameCountForGalvoScan(motion.totalRotationAngleDeg, motion.stepAngleDeg);
            GalvoScanConfig galvoConfig = config.galvo;
            Logger::instance().info(
                "ReconstructionCapture",
                "Galvo controller mode=" + toString(galvoConfig.syncMode) +
                    ", camera mode=" + (useHardwareTrigger ? "hardware-trigger" : "free-run (software capture)"));

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
            if (useHardwareTrigger) {
                if (!leftCamera.validateHardwareTriggerInterval(galvoConfig.captureIntervalMs)) {
                    throw std::runtime_error("Left camera trigger timing invalid: " + leftCamera.lastError());
                }
                if (!rightCamera.validateHardwareTriggerInterval(galvoConfig.captureIntervalMs)) {
                    throw std::runtime_error("Right camera trigger timing invalid: " + rightCamera.lastError());
                }
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

            cancellation.check();
            laserGuard.turnOn();
            cancellation.wait(std::max(200, config.galvo.continuousCaptureWaitMs));

            // 无论相机由外部脉冲还是自由取流，本次重建都必须让振镜以步进角、总旋转角和时间间隔连续扫描。
            // 相机抓图模式仅改变取帧方式，不能把“方向”命令当作每帧的单步运动命令。
            const auto startScan = [&]() {
                cancellation.check();
                const auto continuousCaptureResult = galvoController.startContinuousCapture();
                if (!continuousCaptureResult.success) {
                    throw std::runtime_error(
                        continuousCaptureResult.message.empty()
                            ? "Failed to start galvo continuous reconstruction scan."
                            : continuousCaptureResult.message);
                }
                Logger::instance().info("ReconstructionCapture", useHardwareTrigger
                    ? "Continuous galvo scan command returned; both cameras, receive threads and pair writer were ready before sending. Shared Line5 pulses must trigger one frame per step on each camera."
                    : "Continuous scan command sent; cameras remain in free-run mode.");
            };

            if (useHardwareTrigger) {
                // 扫描前任何图像都是意外脉冲，不能当作本次扫描的第一帧。
                if (!leftCamera.grabFrameWithMetadata(0).image.empty() || !rightCamera.grabFrameWithMetadata(0).image.empty()) {
                    throw std::runtime_error("Unexpected hardware trigger frame before starting the scan. Check trigger line noise or another active scan.");
                }
                std::ostringstream frameAudit;
                frameAudit << "index\tleft_frame_number\tright_frame_number\tleft_trigger_count\tright_trigger_count\tleft_host_timestamp\tright_host_timestamp\n";
                HardwareCaptureOptions options;
                options.cancellation = cancellation;
                options.expectedFrameCount = cameraConfig.frameCount;
                options.frameTimeoutMs = std::max(cameraConfig.leftParameters.grabTimeoutMs, cameraConfig.rightParameters.grabTimeoutMs);
                HardwareTriggeredStereoCapture receiver;
                const auto report = receiver.capture(options,
                    [&](int timeoutMs) { return leftCamera.grabFrameWithMetadata(timeoutMs); },
                    [&](int timeoutMs) { return rightCamera.grabFrameWithMetadata(timeoutMs); },
                    startScan,
                    [&](const HardwareStereoFrame& captured) {
                        FramePair pair;
                        pair.frameIndex = captured.frameIndex;
                        pair.left = captured.left.image;
                        pair.right = captured.right.image;
                        const bool saved = saveFramePair(result, pair, pair.frameIndex);
                        logCapturedFrameProgress(pair.frameIndex, cameraConfig.frameCount, pair, saved);
                        if (!saved) {
                            throw std::runtime_error("Failed to save stereo pair " + std::to_string(pair.frameIndex + 1) + "; capture is incomplete.");
                        }
                        frameAudit << pair.frameIndex + 1 << '\t' << captured.left.frameNumber << '\t' << captured.right.frameNumber << '\t'
                            << captured.left.triggerIndex << '\t' << captured.right.triggerIndex << '\t'
                            << captured.left.hostTimestamp << '\t' << captured.right.hostTimestamp << '\n';
                        if (pair.frameIndex == 0 || (pair.frameIndex + 1) % 10 == 0 || pair.frameIndex + 1 == cameraConfig.frameCount) {
                            Logger::instance().debug("ReconstructionCapture", "Hardware pair " + std::to_string(pair.frameIndex + 1) +
                                ": leftFrame=" + std::to_string(captured.left.frameNumber) + ", rightFrame=" + std::to_string(captured.right.frameNumber) +
                                ", leftTrigger=" + std::to_string(captured.left.triggerIndex) + ", rightTrigger=" + std::to_string(captured.right.triggerIndex));
                        }
                        if (progressCallback) {
                            progressCallback(pair.frameIndex + 1, cameraConfig.frameCount, pair);
                        }
                    });
                const auto laserOffResult = laserGuard.close();
                std::ostringstream timing;
                timing << std::fixed << std::setprecision(2)
                    << "scanStartMs=" << report.scanStartMs
                    << ", firstPairSaveLatencyMs=" << report.firstPairSaveLatencyMs
                    << ", averageSavePairMs=" << (report.savedPairs > 0 ? report.totalSaveMs / report.savedPairs : 0.0)
                    << ", maxSavePairMs=" << report.maxSavePairMs
                    << ", leftPeakQueuedMiB=" << report.leftPeakQueuedBytes / (1024.0 * 1024.0)
                    << ", rightPeakQueuedMiB=" << report.rightPeakQueuedBytes / (1024.0 * 1024.0);
                Logger::instance().info("ReconstructionCapture", "Hardware capture timing: " + timing.str());
                result.success = report.success && result.capturedFrameCount == cameraConfig.frameCount && result.failedFrameCount == 0;
                result.failedFrameCount = std::max(result.failedFrameCount, cameraConfig.frameCount - result.capturedFrameCount);
                const std::string counts = "expected=" + std::to_string(cameraConfig.frameCount) +
                    ", leftReceived=" + std::to_string(report.leftReceived) + ", rightReceived=" + std::to_string(report.rightReceived) +
                    ", savedPairs=" + std::to_string(result.capturedFrameCount);
                result.message = result.success ? "Hardware reconstruction capture complete: " + counts
                    : "Hardware reconstruction capture INCOMPLETE: " + counts + ". " + report.message;
                if (!laserOffResult.success) {
                    result.success = false;
                    result.message += ". Laser OFF command failed; check physical laser state: " + laserOffResult.message;
                }
                frameAudit << "\n" << result.message << "\n" << timing.str() << "\n"
                    << "Note: zero trigger counts may mean unsupported metadata. Frame continuity/counts do not measure mirror position or certify electrical pulse timing.\n";
                QSaveFile auditFile(QDir(QString::fromStdString(result.sessionDirectory)).filePath("frame_audit.txt"));
                const std::string auditText = frameAudit.str();
                if (!auditFile.open(QIODevice::WriteOnly) ||
                    auditFile.write(auditText.data(), static_cast<qint64>(auditText.size())) != static_cast<qint64>(auditText.size()) || !auditFile.commit()) {
                    result.success = false;
                    result.message += ". Failed to save frame_audit.txt.";
                }
                Logger::instance().info("ReconstructionCapture", "Hardware frame count check: " + counts);
            } else {
                startScan();
                int consecutiveLiveTimeouts = 0;
                auto nextSoftwareCapture = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(std::max(1, galvoConfig.continuousCaptureWaitMs));
                for (int frameIndex = 0; frameIndex < config.stereoCamera.frameCount; ++frameIndex) {
                    cancellation.wait(static_cast<int>(std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(nextSoftwareCapture - std::chrono::steady_clock::now()).count())));
                    nextSoftwareCapture += std::chrono::milliseconds(std::max(1, galvoConfig.captureIntervalMs));
                    FramePair pair;
                    pair.frameIndex = frameIndex;
                    pair.left = leftCamera.grabFrame(cameraConfig.leftParameters.grabTimeoutMs);
                    pair.right = rightCamera.grabFrame(cameraConfig.rightParameters.grabTimeoutMs);
                    const bool saved = saveFramePair(result, pair, frameIndex);
                    logCapturedFrameProgress(frameIndex, config.stereoCamera.frameCount, pair, saved);
                    if (saved) {
                        consecutiveLiveTimeouts = 0;
                    } else if (pair.left.empty() || pair.right.empty()) {
                        if (++consecutiveLiveTimeouts >= kMaxConsecutiveLiveTimeouts) {
                            result.message = "Reconstruction capture stopped because cameras did not provide valid live frames. "
                                "Check camera connection, exposure, and camera streaming state.";
                            Logger::instance().error("ReconstructionCapture", result.message);
                            break;
                        }
                    }
                    if (progressCallback) {
                        progressCallback(frameIndex + 1, config.stereoCamera.frameCount, pair);
                    }
                }
            }

            const auto laserOffResult = laserGuard.close();
            if (!laserOffResult.success) {
                result.success = false;
                result.message += ". Laser OFF command failed; check physical laser state: " + laserOffResult.message;
            }
            leftCamera.stopGrabbing();
            rightCamera.stopGrabbing();
            leftCamera.disconnect();
            rightCamera.disconnect();
        } catch (...) {
            // laserGuard 在进入这里之前已析构，先尝试关激光，再释放串口。
            galvoController.disconnect();
            throw;
        }
        galvoController.disconnect();
#else
        throw std::runtime_error("Hik camera support is disabled. Enable HTMSR_ENABLE_HIK_CAMERA or use mock provider.");
#endif
    }

    cancellation.check();
    if (result.message.empty()) {
        result.success = result.capturedFrameCount == config.stereoCamera.frameCount && result.failedFrameCount == 0;
        result.message = result.success
            ? "Reconstruction capture finished, frames=" + std::to_string(result.capturedFrameCount)
            : "Reconstruction capture incomplete: expected=" + std::to_string(config.stereoCamera.frameCount) +
                ", savedPairs=" + std::to_string(result.capturedFrameCount) + ", failed=" + std::to_string(result.failedFrameCount);
    }
    if (result.success) {
        Logger::instance().info("ReconstructionCapture", result.message + ", directory=" + result.sessionDirectory);
    } else {
        Logger::instance().error("ReconstructionCapture", result.message + ", directory=" + result.sessionDirectory);
    }
    return result;
}

} // namespace htmsr::app
