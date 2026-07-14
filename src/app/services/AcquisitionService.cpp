#include "app/services/AcquisitionService.h"

#include "app/acquisition/HikStereoCameraProvider.h"
#include "app/acquisition/MockAcquisitionProvider.h"
#include "core/Logger.h"

#if HTMSR_WITH_HIK_CAMERA
#include "app/acquisition/HikCameraDevice.h"
#endif

#include <QDateTime>
#include <QDir>

#include <opencv2/imgcodecs.hpp>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace htmsr::app {
namespace {

std::string frameFileName(int frameIndex)
{
    std::ostringstream stream;
    stream << "frame_" << std::setw(6) << std::setfill('0') << (frameIndex + 1) << ".bmp";
    return stream.str();
}

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

QString createSessionDirectory(const std::string& outputDirectory)
{
    const QString root = QString::fromStdString(outputDirectory.empty() ? "." : outputDirectory);
    ensureDirectory(root);

    const QString sessionName = "capture_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString sessionDirectory = QDir(root).filePath(sessionName);
    ensureDirectory(sessionDirectory);
    ensureDirectory(QDir(sessionDirectory).filePath("left"));
    ensureDirectory(QDir(sessionDirectory).filePath("right"));
    return sessionDirectory;
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

} // namespace

std::vector<CameraDeviceInfo> AcquisitionService::enumerateDevices() const
{
#if HTMSR_WITH_HIK_CAMERA
    return enumerateHikCameraDevices();
#else
    Logger::instance().warning("Acquisition", "Hik camera support is disabled in this build.");
    return {};
#endif
}

AcquisitionSessionResult AcquisitionService::capture(const StereoCameraConfig& config, ProgressCallback progressCallback) const
{
    if (config.frameCount <= 0) {
        throw std::runtime_error("Capture frame count must be greater than zero.");
    }

    AcquisitionSessionResult result;
    const QString sessionDirectory = createSessionDirectory(config.outputDirectory);
    result.sessionDirectory = sessionDirectory.toStdString();
    result.leftDirectory = QDir(sessionDirectory).filePath("left").toStdString();
    result.rightDirectory = QDir(sessionDirectory).filePath("right").toStdString();

    Logger::instance().info("CaptureSession", "Capture session created: " + result.sessionDirectory);
    auto provider = createProvider(config);

    for (int frameIndex = 0; frameIndex < config.frameCount && provider->hasNext(); ++frameIndex) {
        FramePair pair = provider->next();
        const QString fileName = QString::fromStdString(frameFileName(frameIndex));
        const QString leftPath = QDir(QString::fromStdString(result.leftDirectory)).filePath(fileName);
        const QString rightPath = QDir(QString::fromStdString(result.rightDirectory)).filePath(fileName);

        if (pair.left.empty() || pair.right.empty()) {
            ++result.failedFrameCount;
            Logger::instance().warning("CaptureSession", "Captured empty frame pair, frame=" + std::to_string(frameIndex + 1));
            continue;
        }

        const bool leftSaved = cv::imwrite(leftPath.toStdString(), pair.left);
        const bool rightSaved = cv::imwrite(rightPath.toStdString(), pair.right);
        if (!leftSaved || !rightSaved) {
            ++result.failedFrameCount;
            Logger::instance().error("CaptureSession", "Failed to save frame pair, frame=" + std::to_string(frameIndex + 1));
            continue;
        }

        result.leftImagePaths.push_back(leftPath.toStdString());
        result.rightImagePaths.push_back(rightPath.toStdString());
        result.lastLeftPreview = pair.left.clone();
        result.lastRightPreview = pair.right.clone();
        ++result.capturedFrameCount;

        if (progressCallback) {
            progressCallback(frameIndex + 1, config.frameCount);
        }
    }

    result.success = result.capturedFrameCount > 0;
    result.message = result.success
        ? "Capture finished, frames=" + std::to_string(result.capturedFrameCount)
        : "Capture finished without valid frames.";
    Logger::instance().info("CaptureSession", result.message + ", directory=" + result.sessionDirectory);
    return result;
}

AcquisitionProviderPtr AcquisitionService::createProvider(const StereoCameraConfig& config) const
{
    if (config.useMockProvider) {
        Logger::instance().info("Acquisition", "Using mock stereo acquisition provider.");
        return std::make_unique<MockAcquisitionProvider>(config.frameCount);
    }

#if HTMSR_WITH_HIK_CAMERA
    const auto devices = enumerateHikCameraDevices();
    const auto leftInfo = findDeviceById(devices, config.leftDeviceId);
    const auto rightInfo = findDeviceById(devices, config.rightDeviceId);
    return std::make_unique<HikStereoCameraProvider>(
        std::make_unique<HikCameraDevice>(leftInfo),
        std::make_unique<HikCameraDevice>(rightInfo),
        config);
#else
    throw std::runtime_error("Hik camera support is disabled. Enable HTMSR_ENABLE_HIK_CAMERA or use mock provider.");
#endif
}

} // namespace htmsr::app
