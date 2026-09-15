#include "app/services/CalibrationCaptureSessionService.h"

#include "app/acquisition/HikStereoCameraProvider.h"
#include "app/acquisition/MockAcquisitionProvider.h"
#include "core/Logger.h"

#if HTMSR_WITH_HIK_CAMERA
#include "app/acquisition/HikCameraDevice.h"
#endif

#include <QDateTime>
#include <QDir>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
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
    const QString captureRoot = QDir(root).filePath("calibration/capture");
    ensureDirectory(captureRoot);

    const QString sessionName = "calibration_capture_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
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
    std::ostringstream stream;
    stream << "frame_" << std::setw(6) << std::setfill('0') << (frameIndex + 1) << ".bmp";
    return stream.str();
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

struct CalibrationCaptureSessionService::Impl {
    StereoCameraConfig config;
    AcquisitionSessionResult result;
    bool active = false;
    int nextFrameIndex = 0;
    std::mutex mutex;
    AcquisitionProviderPtr mockProvider;
    AcquisitionProviderPtr mockPreviewProvider;
#if HTMSR_WITH_HIK_CAMERA
    CameraDevicePtr leftDevice;
    CameraDevicePtr rightDevice;
#endif
};

CalibrationCaptureSessionService::CalibrationCaptureSessionService()
    : impl_(std::make_unique<Impl>())
{
}

CalibrationCaptureSessionService::~CalibrationCaptureSessionService()
{
    finish();
}

CalibrationCaptureSessionState CalibrationCaptureSessionService::start(const StereoCameraConfig& config)
{
    finish();

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->config = config;
    impl_->config.frameCount = std::max(1, config.frameCount);
    impl_->config.leftParameters.useHardwareTrigger = false;
    impl_->config.rightParameters.useHardwareTrigger = false;
    impl_->result = createSession(impl_->config.outputDirectory);
    impl_->nextFrameIndex = 0;

    if (impl_->config.useMockProvider) {
        impl_->mockProvider = std::make_unique<MockAcquisitionProvider>(impl_->config.frameCount);
        impl_->mockPreviewProvider = std::make_unique<MockAcquisitionProvider>(std::numeric_limits<int>::max());
        Logger::instance().info("CalibrationCapture", "Started mock calibration capture session: " + impl_->result.sessionDirectory);
    } else {
#if HTMSR_WITH_HIK_CAMERA
        const auto devices = enumerateHikCameraDevices();
        impl_->leftDevice = std::make_unique<HikCameraDevice>(findDeviceById(devices, impl_->config.leftDeviceId));
        impl_->rightDevice = std::make_unique<HikCameraDevice>(findDeviceById(devices, impl_->config.rightDeviceId));

        if (!impl_->leftDevice->connect() || !impl_->rightDevice->connect()) {
            finish();
            throw std::runtime_error("Failed to connect Hik stereo cameras.");
        }
        if (!impl_->leftDevice->configure(impl_->config.leftParameters) || !impl_->rightDevice->configure(impl_->config.rightParameters)) {
            finish();
            throw std::runtime_error("Failed to configure Hik stereo cameras.");
        }
        if (!impl_->leftDevice->startGrabbing() || !impl_->rightDevice->startGrabbing()) {
            finish();
            throw std::runtime_error("Failed to start Hik stereo grabbing.");
        }
        Logger::instance().info("CalibrationCapture", "Started calibration capture session: " + impl_->result.sessionDirectory);
#else
        finish();
        throw std::runtime_error("Hik camera support is disabled. Enable HTMSR_ENABLE_HIK_CAMERA or use mock provider.");
#endif
    }

    impl_->active = true;
    impl_->result.success = true;
    impl_->result.message = "Calibration capture session started.";

    CalibrationCaptureSessionState state;
    state.acquisition = impl_->result;
    state.active = impl_->active;
    state.camerasReady = impl_->active;
    return state;
}

AcquisitionSessionResult CalibrationCaptureSessionService::captureCurrentFrame()
{
    if (!impl_->active) {
        throw std::runtime_error("Calibration capture session is not active.");
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    FramePair pair;
    pair.frameIndex = impl_->nextFrameIndex;
    if (impl_->config.useMockProvider) {
        pair = impl_->mockProvider->next();
    } else {
#if HTMSR_WITH_HIK_CAMERA
        pair.left = impl_->leftDevice->grabFrame(impl_->config.leftParameters.grabTimeoutMs);
        pair.right = impl_->rightDevice->grabFrame(impl_->config.rightParameters.grabTimeoutMs);
#endif
    }

    const QString fileName = QString::fromStdString(frameFileName(impl_->nextFrameIndex));
    const QString leftPath = QDir(QString::fromStdString(impl_->result.leftDirectory)).filePath(fileName);
    const QString rightPath = QDir(QString::fromStdString(impl_->result.rightDirectory)).filePath(fileName);

    if (pair.left.empty() || pair.right.empty()) {
        ++impl_->result.failedFrameCount;
        impl_->result.message = "Captured empty calibration frame pair.";
        Logger::instance().warning("CalibrationCapture", impl_->result.message);
        return impl_->result;
    }

    if (!cv::imwrite(leftPath.toStdString(), pair.left) || !cv::imwrite(rightPath.toStdString(), pair.right)) {
        ++impl_->result.failedFrameCount;
        impl_->result.message = "Failed to save calibration frame pair.";
        Logger::instance().error("CalibrationCapture", impl_->result.message);
        return impl_->result;
    }

    impl_->result.leftImagePaths.push_back(leftPath.toStdString());
    impl_->result.rightImagePaths.push_back(rightPath.toStdString());
    impl_->result.lastLeftPreview = pair.left.clone();
    impl_->result.lastRightPreview = pair.right.clone();
    ++impl_->result.capturedFrameCount;
    ++impl_->nextFrameIndex;
    impl_->result.success = impl_->result.capturedFrameCount > 0;
    impl_->result.message = "Calibration frame captured, frames=" + std::to_string(impl_->result.capturedFrameCount);
    Logger::instance().info("CalibrationCapture", impl_->result.message);
    return impl_->result;
}

FramePair CalibrationCaptureSessionService::grabPreviewFrame()
{
    if (!impl_ || !impl_->active) {
        return {};
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    FramePair pair;
    if (impl_->config.useMockProvider) {
        if (!impl_->mockPreviewProvider) {
            impl_->mockPreviewProvider = std::make_unique<MockAcquisitionProvider>(std::numeric_limits<int>::max());
        }
        pair = impl_->mockPreviewProvider->next();
    } else {
#if HTMSR_WITH_HIK_CAMERA
        pair.left = impl_->leftDevice ? impl_->leftDevice->grabFrame(impl_->config.leftParameters.grabTimeoutMs) : cv::Mat{};
        pair.right = impl_->rightDevice ? impl_->rightDevice->grabFrame(impl_->config.rightParameters.grabTimeoutMs) : cv::Mat{};
#endif
    }
    return pair;
}

void CalibrationCaptureSessionService::finish()
{
    if (!impl_) {
        return;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
#if HTMSR_WITH_HIK_CAMERA
    if (impl_->leftDevice) {
        impl_->leftDevice->stopGrabbing();
        impl_->leftDevice->disconnect();
        impl_->leftDevice.reset();
    }
    if (impl_->rightDevice) {
        impl_->rightDevice->stopGrabbing();
        impl_->rightDevice->disconnect();
        impl_->rightDevice.reset();
    }
#endif
    impl_->mockProvider.reset();
    impl_->mockPreviewProvider.reset();
    if (impl_->active) {
        Logger::instance().info("CalibrationCapture", "Calibration capture session finished.");
    }
    impl_->active = false;
}

bool CalibrationCaptureSessionService::isActive() const
{
    return impl_ && impl_->active;
}

AcquisitionSessionResult CalibrationCaptureSessionService::currentResult() const
{
    if (!impl_) {
        return {};
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_ ? impl_->result : AcquisitionSessionResult{};
}

void CalibrationCaptureSessionService::setCurrentResult(const AcquisitionSessionResult& result)
{
    if (!impl_) {
        return;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->result = result;
    impl_->result.capturedFrameCount = static_cast<int>(std::min(
        impl_->result.leftImagePaths.size(),
        impl_->result.rightImagePaths.size()));
    impl_->result.success = impl_->active || impl_->result.capturedFrameCount > 0;
}

} // namespace htmsr::app
