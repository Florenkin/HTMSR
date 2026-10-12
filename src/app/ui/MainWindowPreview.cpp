#include "app/ui/MainWindowTasks.h"

namespace htmsr::app {
using namespace detail;

void MainWindow::enqueueLivePreview(const FramePair& frame)
{
    if (shuttingDown_.load()) {
        return;
    }

    cv::Mat left = frame.left.clone();
    cv::Mat right = frame.right.clone();
    QMetaObject::invokeMethod(
        this,
        [this, left = std::move(left), right = std::move(right)]() mutable {
            if (shuttingDown_.load()) {
                return;
            }
            setLiveStereoImages(left, right);
        },
        Qt::QueuedConnection);
}

void MainWindow::setLiveStereoImages(const cv::Mat& leftImage, const cv::Mat& rightImage)
{
    if (!leftImage.empty() && liveLeftImageView_) {
        liveLeftImageView_->setImage(leftImage);
    }
    if (!rightImage.empty() && liveRightImageView_) {
        liveRightImageView_->setImage(rightImage);
    }
    if (!leftImage.empty() || !rightImage.empty()) {
        updateLiveFps();
    }
}

void MainWindow::updateLiveFps()
{
    if (!liveFpsLabel_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (liveFpsWindowStart_.time_since_epoch().count() == 0) {
        liveFpsWindowStart_ = now;
        liveFpsFrameCount_ = 0;
    }

    ++liveFpsFrameCount_;
    const double elapsedSeconds = std::chrono::duration<double>(now - liveFpsWindowStart_).count();
    if (elapsedSeconds < 1.0) {
        return;
    }

    const double fps = static_cast<double>(liveFpsFrameCount_) / elapsedSeconds;
    liveFpsLabel_->setText(QString::fromUtf8("帧率: %1 FPS").arg(fps, 0, 'f', 1));
    liveFpsWindowStart_ = now;
    liveFpsFrameCount_ = 0;
}

void MainWindow::resetLiveFps()
{
    liveFpsWindowStart_ = std::chrono::steady_clock::now();
    liveFpsFrameCount_ = 0;
    if (liveFpsLabel_) {
        liveFpsLabel_->setText(QString::fromUtf8("帧率: -- FPS"));
    }
}

void MainWindow::startLivePreview()
{
    if (shuttingDown_.load() || !acquisitionPanel_ || busy_ || livePreviewWatcher_.isRunning()) {
        return;
    }

    if (calibrationCaptureSessionService_.isActive()) {
        resetLiveFps();
        livePreviewStopRequested_.store(false);
        livePreviewWatcher_.setFuture(QtConcurrent::run([this]() {
            runCalibrationCapturePreviewLoop();
        }));
        return;
    }

    StereoCameraConfig config = acquisitionPanel_->stereoCameraConfig();
    config.frameCount = std::numeric_limits<int>::max();
    config.leftParameters.useHardwareTrigger = false;
    config.rightParameters.useHardwareTrigger = false;

    if (!config.useMockProvider) {
        if (config.leftDeviceId.empty() || config.rightDeviceId.empty()) {
            return;
        }
        if (config.leftDeviceId == config.rightDeviceId) {
            acquisitionPanel_->setStatusText(QString::fromUtf8("实时预览需要选择两台不同的相机。"));
            return;
        }
    }

    resetLiveFps();
    livePreviewStopRequested_.store(false);
    livePreviewWatcher_.setFuture(QtConcurrent::run([this, config]() {
        runLivePreviewLoop(config);
    }));
}

void MainWindow::stopLivePreview()
{
    livePreviewStopRequested_.store(true);
    if (livePreviewWatcher_.isRunning()) {
        livePreviewWatcher_.waitForFinished();
    }
}

void MainWindow::restartLivePreview()
{
    if (busy_) {
        return;
    }

    stopLivePreview();
    startLivePreview();
}

void MainWindow::runLivePreviewLoop(StereoCameraConfig config)
{
    try {
        AcquisitionService service;
        auto provider = service.createProvider(config);
        Logger::instance().debug("Acquisition", "Live stereo preview started.");

        while (!shuttingDown_.load() && !livePreviewStopRequested_.load() && provider->hasNext()) {
            FramePair frame = provider->next();
            if (!frame.left.empty() || !frame.right.empty()) {
                enqueueLivePreview(frame);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }

        Logger::instance().debug("Acquisition", "Live stereo preview stopped.");
    } catch (const std::exception& ex) {
        if (!shuttingDown_.load() && !livePreviewStopRequested_.load()) {
            Logger::instance().warning("Acquisition", std::string("Live stereo preview stopped: ") + ex.what());
            QMetaObject::invokeMethod(
                this,
                [this, message = QString::fromStdString(ex.what())]() {
                    if (shuttingDown_.load()) {
                        return;
                    }
                    if (acquisitionPanel_ && !busy_) {
                        acquisitionPanel_->setStatusText(QString::fromUtf8("实时预览未启动：%1").arg(message));
                    }
                },
                Qt::QueuedConnection);
        }
    }
}

void MainWindow::runCalibrationCapturePreviewLoop()
{
    Logger::instance().debug("Acquisition", "Calibration capture live preview started.");
    while (!shuttingDown_.load() && !livePreviewStopRequested_.load() && calibrationCaptureSessionService_.isActive()) {
        FramePair frame = calibrationCaptureSessionService_.grabPreviewFrame();
        if (!frame.left.empty() || !frame.right.empty()) {
            enqueueLivePreview(frame);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    Logger::instance().debug("Acquisition", "Calibration capture live preview stopped.");
}

void MainWindow::refreshLaserSwitchPreview(const IntegratedScanConfig& config)
{
    if (config.stereoCamera.useMockProvider) {
        Logger::instance().warning("Acquisition", "Laser switch preview refresh skipped because mock acquisition is enabled.");
        return;
    }

#if HTMSR_WITH_HIK_CAMERA
    auto cameraConfig = config.stereoCamera;
    cameraConfig.leftParameters.useHardwareTrigger = false;
    cameraConfig.rightParameters.useHardwareTrigger = false;

    const auto devices = enumerateHikCameraDevices();
    HikCameraDevice leftCamera(findSelectedCameraDevice(devices, cameraConfig.leftDeviceId));
    HikCameraDevice rightCamera(findSelectedCameraDevice(devices, cameraConfig.rightDeviceId));

    try {
        if (!leftCamera.connect() || !rightCamera.connect()) {
            throw std::runtime_error("Failed to connect selected Hik cameras for preview refresh.");
        }
        if (!leftCamera.configure(cameraConfig.leftParameters) || !rightCamera.configure(cameraConfig.rightParameters)) {
            throw std::runtime_error("Failed to configure selected Hik cameras for preview refresh.");
        }
        if (!leftCamera.startGrabbing() || !rightCamera.startGrabbing()) {
            throw std::runtime_error("Failed to start selected Hik cameras for preview refresh.");
        }

        FramePair frame;
        frame.left = leftCamera.grabFrame(cameraConfig.leftParameters.grabTimeoutMs);
        frame.right = rightCamera.grabFrame(cameraConfig.rightParameters.grabTimeoutMs);

        leftCamera.stopGrabbing();
        rightCamera.stopGrabbing();
        leftCamera.disconnect();
        rightCamera.disconnect();

        if (frame.left.empty() || frame.right.empty()) {
            throw std::runtime_error("Preview refresh captured an empty camera frame.");
        }

        LaserExtractionService extractor;
        const auto extraction = extractor.extract(frame.left, config.laserConfig.leftRoi, config.laserConfig);
        cv::Mat left = frame.left.clone();
        cv::Mat right = frame.right.clone();
        if (shuttingDown_.load()) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, left = std::move(left), right = std::move(right)]() mutable {
                if (shuttingDown_.load()) {
                    return;
                }
                setLiveStereoImages(left, right);
            },
            Qt::QueuedConnection);
        Logger::instance().info(
            "Acquisition",
            "Laser switch preview frame refreshed, extracted left laser points=" + std::to_string(extraction.points.size()));
    } catch (...) {
        leftCamera.stopGrabbing();
        rightCamera.stopGrabbing();
        leftCamera.disconnect();
        rightCamera.disconnect();
        throw;
    }
#else
    (void)config;
    Logger::instance().warning("Acquisition", "Laser switch preview refresh skipped because Hik camera support is disabled.");
#endif
}

} // namespace htmsr::app
