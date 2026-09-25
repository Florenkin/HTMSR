#include "app/ui/MainWindowTasks.h"

namespace htmsr::app {
using namespace detail;

void MainWindow::runCalibration()
{
    if (busy_ || calibrationWatcher_.isRunning()) {
        return;
    }

    acquisitionPanel_->commitPendingEdits();
    CalibrationInput input = acquisitionPanel_->calibrationInput();
    if (input.leftDirectory.empty() && input.rightDirectory.empty()) {
        const auto captured = calibrationCaptureSessionService_.currentResult();
        if (captured.capturedFrameCount > 0) {
            input.leftDirectory = captured.leftDirectory;
            input.rightDirectory = captured.rightDirectory;
        }
    }
    if (!QFileInfo(QString::fromStdString(input.leftDirectory)).isDir() ||
        !QFileInfo(QString::fromStdString(input.rightDirectory)).isDir()) {
        QMessageBox::warning(this, QString::fromUtf8("缺少标定图像"),
            QString::fromUtf8("请先采集标定帧，或选择有效的左、右标定目录。"));
        return;
    }

    // 每次标定生成新的结果文件，并由 CalibrationService 在成功后自动保存。
    input.outputFile = defaultCalibrationFilePath().toStdString();
    pendingCalibrationInput_ = input;
    finishCalibrationCapture();

    setBusy(true, QString::fromUtf8("标定中..."), true);
    calibrationWatcher_.setFuture(QtConcurrent::run([input, cancellation = shutdownCancellation_]() {
        return runWorkerTask("Calibration", [input, cancellation]() {
            CalibrationService service;
            return service.calibrate(input, cancellation);
        });
    }));
}

void MainWindow::loadCalibration()
{
    const QString file = QFileDialog::getOpenFileName(this, QString::fromUtf8("加载标定文件"), QString(), QString::fromUtf8("YAML (*.yml *.yaml);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    loadCalibrationFile(file);
}

void MainWindow::exportCalibration()
{
    if (busy_) {
        return;
    }
    if (!calibration_.isValid()) {
        QMessageBox::information(this, QString::fromUtf8("无标定结果"), QString::fromUtf8("请先完成标定或加载有效标定文件。"));
        return;
    }
    try {
        const QString file = QFileDialog::getSaveFileName(this, QString::fromUtf8("导出标定结果"),
            defaultCalibrationFilePath(), QString::fromUtf8("标定文件 (*.yml *.yaml)"));
        if (file.isEmpty()) {
            return;
        }
        setBusy(true, QString::fromUtf8("正在导出标定结果..."), true);
        const QString exported = ResultExportService::exportCalibration(file, calibration_);
        setBusy(false, QString());
        statusBar()->showMessage(QString::fromUtf8("标定结果已导出：%1").arg(exported), 10000);
    } catch (const std::exception& ex) {
        setBusy(false, QString());
        Logger::instance().error("Calibration", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("导出失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::exportReconstruction()
{
    if (busy_) {
        return;
    }
    if (!reconstruction_.success || reconstruction_.mergedPoints.empty()) {
        QMessageBox::information(this, QString::fromUtf8("无点云"), QString::fromUtf8("请先完成重建。"));
        return;
    }
    try {
        const QString pcdFilter = QString::fromUtf8("PCD 点云 (*.pcd)");
        const QString txtFilter = QString::fromUtf8("TXT 点云 (*.txt)");
        QString selectedFilter = pcdFilter;
        const QString name = "point_cloud_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".pcd";
        const QString directory = ReconstructionStorage::resultsDirectory(acquisitionPanel_->projectConfig().outputDirectory);
        const QString file = QFileDialog::getSaveFileName(this, QString::fromUtf8("导出点云"),
            QDir(directory).filePath(name), pcdFilter + ";;" + txtFilter, &selectedFilter);
        if (file.isEmpty()) {
            return;
        }
        setBusy(true, QString::fromUtf8("正在导出点云..."), true);
        const auto format = selectedFilter == txtFilter ? PointCloudExportFormat::Txt : PointCloudExportFormat::Pcd;
        const QString exported = ResultExportService::exportPointCloud(file, reconstruction_, format);
        setBusy(false, QString());
        statusBar()->showMessage(QString::fromUtf8("点云已导出：%1").arg(exported), 10000);
    } catch (const std::exception& ex) {
        setBusy(false, QString());
        Logger::instance().error("PointCloud", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("导出失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::refreshAcquisitionDevices()
{
    const auto ports = enumerateSerialPortNames();
    acquisitionPanel_->setSerialPorts(ports);
    if (ports.empty()) {
        Logger::instance().warning("Galvo", "未检测到在线串口。请检查振镜 USB 连接、供电及驱动。");
    } else {
        QStringList names;
        for (const auto& port : ports) {
            names.append(QString::fromStdString(port));
        }
        Logger::instance().info("Galvo", "检测到在线串口：" + names.join(", ").toStdString());
    }
    try {
        const auto devices = acquisitionService_.enumerateDevices();
        acquisitionPanel_->setDevices(devices);
        Logger::instance().info("Acquisition", "刷新设备完成，相机数量：" + std::to_string(devices.size()));
    } catch (const std::exception& ex) {
        Logger::instance().error("Acquisition", ex.what());
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
    }
}

void MainWindow::captureCalibrationFrame()
{
    if (busy_ || calibrationFrameWatcher_.isRunning()) {
        return;
    }

    const StereoCameraConfig config = acquisitionPanel_->stereoCameraConfig();
    if (!config.useMockProvider && (config.leftDeviceId.empty() || config.rightDeviceId.empty() ||
        config.leftDeviceId == config.rightDeviceId)) {
        QMessageBox::warning(this, QString::fromUtf8("无法采集标定帧"), QString::fromUtf8("请先选择两台不同的相机。"));
        return;
    }
    setBusy(true, QString::fromUtf8("采集当前标定帧..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在采集当前标定帧..."));
    calibrationFrameWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Capture calibration frame", [this, config]() {
            return calibrationCaptureSessionService_.captureCurrentFrame(config);
        });
    }));
}

void MainWindow::calibrateCapturedFrames()
{
    runCalibration();
}

void MainWindow::loadCalibrationFile(const QString& file)
{
    if (busy_ || shuttingDown_.load()) {
        return;
    }
    finishCalibrationCapture();
    acquisitionPanel_->setCalibrationFile(file.trimmed().toStdString());
    calibration_ = CalibrationResult{};
    updateResultAvailability();
    if (file.trimmed().isEmpty()) {
        return;
    }
    CalibrationResult loaded;
    if (!calibrationService_.loadCalibration(file.trimmed().toStdString(), loaded)) {
        QMessageBox::warning(this, QString::fromUtf8("加载失败"), QString::fromUtf8("标定文件无效或无法读取。"));
        return;
    }

    calibration_ = loaded;
    updateResultAvailability();
    configService_.save(acquisitionPanel_->projectConfig());
    acquisitionPanel_->setStatusText(QString::fromUtf8("已加载标定结果：%1").arg(file));
    acquisitionPanel_->setResultSummary(QString::fromUtf8("标定文件: %1 | RMS: %2").arg(QFileInfo(file).fileName()).arg(calibration_.rms));

    Logger::instance().info("Calibration", "Calibration result loaded from selected file: " + file.toStdString());
    statusBar()->showMessage(QString::fromUtf8("已加载标定文件：%1 | RMS: %2").arg(file).arg(calibration_.rms));
}

void MainWindow::finishCalibrationCapture()
{
    if (busy_ || !calibrationCaptureSessionService_.isActive()) {
        return;
    }
    stopLivePreview();
    calibrationCaptureSessionService_.finish();
    calibrationCapture_ = calibrationCaptureSessionService_.currentResult();
    acquisition_ = calibrationCapture_;
    acquisitionPanel_->setCalibrationCaptureState(false, calibrationCapture_.capturedFrameCount);
    acquisitionPanel_->setStatusText(QString::fromUtf8("标定采集已结束，已采集 %1 帧。").arg(calibrationCapture_.capturedFrameCount));
    setCaptureReviewResult(calibrationCapture_);
    startLivePreview();
}

void MainWindow::startReconstructionCapture()
{
    if (reconstructionCaptureWatcher_.isRunning() || reconstructionGalvoPreflightWatcher_.isRunning()) {
        return;
    }
    if (galvoMotionParametersWatcher_.isRunning()) {
        acquisitionPanel_->setStatusText(QString::fromUtf8("振镜运动参数正在下发，请稍后再开始重建采集。"));
        return;
    }

    IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    const auto projectConfig = acquisitionPanel_->projectConfig();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = projectConfig.outputDirectory;
    }
    if (config.stereoCamera.frameCount <= 0) {
        const QString message = QString::fromUtf8("总旋转角度和步进角度无法得到有效采集帧数，请检查振镜运动参数。");
        acquisitionPanel_->setStatusText(message);
        QMessageBox::warning(this, QString::fromUtf8("无法开始重建采集"), message);
        return;
    }

    pendingReconstructionCaptureConfig_ = config;
    setBusy(true, QString::fromUtf8("正在重新设置并确认振镜参数..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在连接振镜，重新设置步进角度和自动旋转角度，然后回读验证..."));
    reconstructionGalvoPreflightWatcher_.setFuture(QtConcurrent::run([galvoConfig = config.galvo, cancellation = shutdownCancellation_]() {
        return configureAndVerifyGalvoMotionParameters(galvoConfig, cancellation);
    }));
}

void MainWindow::onReconstructionGalvoPreflightFinished()
{
    if (shuttingDown_.load()) return;
    const auto result = reconstructionGalvoPreflightWatcher_.result();
    auto config = pendingReconstructionCaptureConfig_;

    acquisitionPanel_->setStatusText(result.message);
    updateGalvoStatusBar(config, result.message);
    if (!result.success) {
        setBusy(false, QString());
        Logger::instance().error("Galvo", result.message.toStdString());
        QMessageBox::warning(this, QString::fromUtf8("重建前振镜检查失败"), result.message);
        pendingReconstructionCaptureConfig_ = {};
        return;
    }

    config.stereoCamera.frameCount = frameCountForGalvoScan(result.actualTotalAngleDeg, result.actualStepAngleDeg);
    if (config.stereoCamera.frameCount <= 0) {
        const QString message = QString::fromUtf8("设备回读的总旋转角度或步进角度无效，无法计算采集帧数。");
        setBusy(false, QString());
        acquisitionPanel_->setStatusText(message);
        Logger::instance().error("Galvo", message.toStdString());
        QMessageBox::warning(this, QString::fromUtf8("重建前振镜检查失败"), message);
        pendingReconstructionCaptureConfig_ = {};
        return;
    }
    config.totalRotationAngleDeg = result.actualTotalAngleDeg;
    config.galvo.autoRotationAngleDeg = result.actualTotalAngleDeg;
    config.galvo.stepAngleDeg = result.actualStepAngleDeg;
    config.verifiedGalvoMotion = VerifiedGalvoMotionParameters{
        result.portName.toStdString(), result.actualStepAngleDeg, result.actualTotalAngleDeg};
    acquisitionPanel_->setFrameCountFromDevice(result.actualStepAngleDeg, result.actualTotalAngleDeg);
    Logger::instance().info("Galvo", (result.message + QString::fromUtf8("，本次采集 %1 帧").arg(config.stereoCamera.frameCount)).toStdString());
    beginReconstructionCapture(config);
    pendingReconstructionCaptureConfig_ = {};
}

void MainWindow::beginReconstructionCapture(const IntegratedScanConfig& config)
{
    statusBar()->showMessage(QString::fromUtf8("重建采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在采集重建图像序列..."));
    reconstructionCaptureWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Reconstruction capture", [this, config]() {
            return reconstructionCaptureSessionService_.capture(config, [this](int, int, const FramePair& frame) {
                enqueueLivePreview(frame);
            }, shutdownCancellation_);
        });
    }));
}

void MainWindow::reconstructCapturedFrames()
{
    try {
        if (reconstructionWatcher_.isRunning()) {
            return;
        }

        ReconstructionInput input = acquisitionPanel_->reconstructionInput(calibration_);
        if (input.leftDirectory.empty() || input.rightDirectory.empty()) {
            QMessageBox::warning(this, QString::fromUtf8("缺少重建目录"), QString::fromUtf8("请选择左右重建目录，或先点击重建页的“采集”。"));
            return;
        }
        const QFileInfo leftDirectory(QString::fromStdString(input.leftDirectory));
        const QFileInfo rightDirectory(QString::fromStdString(input.rightDirectory));
        if (!leftDirectory.isDir() || !rightDirectory.isDir()) {
            QMessageBox::warning(this, QString::fromUtf8("重建目录无效"),
                QString::fromUtf8("请选择存在的左右重建图像目录。"));
            return;
        }

        const AppProjectConfig projectConfig = acquisitionPanel_->projectConfig();
        if (projectConfig.saveLaserExtractionImages && projectConfig.laserExtractionDirectory.empty()) {
            QMessageBox::warning(this, QString::fromUtf8("缺少激光线目录"),
                QString::fromUtf8("启用保存激光线图片时，请先选择激光线目录。"));
            return;
        }

        if (!calibration_.isValid()) {
            const auto file = projectConfig.calibrationFile;
            calibrationService_.loadCalibration(file, calibration_);
        }
        if (!calibration_.isValid()) {
            QMessageBox::warning(this, QString::fromUtf8("缺少标定"), QString::fromUtf8("请先完成标定或加载有效标定文件。"));
            return;
        }

        input.calibration = calibration_;
        const auto outputDirectory = projectConfig.outputDirectory;
        Logger::instance().info("Reconstruction", "重建图像目录：左=" + input.leftDirectory + "，右=" + input.rightDirectory);
        const std::string captureSessionDirectory = leftDirectory.absolutePath() == rightDirectory.absolutePath()
            ? leftDirectory.absolutePath().toStdString() : std::string{};
        laserExtractionReviewWidget_->clearImages();
        setBusy(true, QString::fromUtf8("重建当前采集帧..."), true);
        acquisitionPanel_->setStatusText(QString::fromUtf8("正在重建当前采集帧..."));
        const std::string laserExtractionDirectory = projectConfig.laserExtractionDirectory;
        const bool saveLaserExtractionImages = projectConfig.saveLaserExtractionImages;
        const CancellationToken cancellation = shutdownCancellation_;
        reconstructionWatcher_.setFuture(QtConcurrent::run([
            input, outputDirectory, captureSessionDirectory, laserExtractionDirectory,
            saveLaserExtractionImages, cancellation]() {
            return runReconstructionTask(input, outputDirectory, captureSessionDirectory,
                laserExtractionDirectory, saveLaserExtractionImages, cancellation);
        }));
    } catch (const cv::Exception& ex) {
        setBusy(false, QString());
        Logger::instance().error("Reconstruction", std::string("Failed to start captured-frame reconstruction: ") + ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("启动当前采集帧重建时发生 OpenCV 异常：%1").arg(QString::fromStdString(ex.what())));
    } catch (const std::exception& ex) {
        setBusy(false, QString());
        Logger::instance().error("Reconstruction", std::string("Failed to start captured-frame reconstruction: ") + ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromStdString(ex.what()));
    } catch (...) {
        setBusy(false, QString());
        Logger::instance().error("Reconstruction", "Failed to start captured-frame reconstruction with an unknown exception.");
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("启动当前采集帧重建时发生未知异常。"));
    }
}

void MainWindow::onCalibrationFinished()
{
    if (shuttingDown_.load()) return;
    setBusy(false, QString());
    try {
        calibration_ = calibrationWatcher_.result();
        updateResultAvailability();
        acquisitionPanel_->setCalibrationDirectories(pendingCalibrationInput_.leftDirectory, pendingCalibrationInput_.rightDirectory);
        acquisitionPanel_->setCalibrationFile(pendingCalibrationInput_.outputFile);
        configService_.save(acquisitionPanel_->projectConfig());
        acquisitionPanel_->setResultSummary(QString::fromUtf8("RMS: %1 | 标定帧: %2")
            .arg(calibration_.rms).arg(calibration_.successfulPairs));

        QMessageBox::information(this, QString::fromUtf8("标定完成"),
            QString::fromUtf8("双目标定完成。RMS: %1\n标定结果已自动保存到：\n%2")
                .arg(calibration_.rms).arg(QString::fromStdString(pendingCalibrationInput_.outputFile)));
    } catch (const std::exception& ex) {
        Logger::instance().error("Calibration", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("标定失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onReconstructionFinished()
{
    if (shuttingDown_.load()) return;
    setBusy(false, QString());
    try {
        auto taskResult = reconstructionWatcher_.result();
        const auto& laserImages = taskResult.laserExtractionImages;
        if (laserImages.enabled) {
            laserExtractionReviewWidget_->setImagePairs(
                laserImages.sessionDirectory, laserImages.leftImagePaths, laserImages.rightImagePaths);
        }

        QString laserWarning;
        if (!laserImages.warningMessage.empty() || laserImages.failedPairCount > 0) {
            laserWarning = QString::fromUtf8("\n\n激光线图片保存警告：失败 %1 对。%2")
                .arg(laserImages.failedPairCount)
                .arg(QString::fromStdString(laserImages.warningMessage));
            Logger::instance().warning("LaserExtraction", laserWarning.toStdString());
        }

        auto completed = std::move(taskResult.reconstruction);
        if (!completed.success || completed.mergedPoints.empty()) {
            QString message = completed.message.empty()
                ? QString::fromUtf8("没有重建出有效点云，请检查标定文件、ROI、激光阈值和采集图像。")
                : QString::fromStdString(completed.message);
            message += laserWarning;
            Logger::instance().warning("Reconstruction", message.toStdString());
            QMessageBox::warning(this, QString::fromUtf8("重建失败"), message);
            return;
        }
        reconstruction_ = std::move(completed);
        updateResultAvailability();
        pointCloudView_->setPoints(reconstruction_.mergedPoints);

        QMessageBox::information(this, QString::fromUtf8("重建完成"),
            QString::fromUtf8("点云数量: %1\n已自动保存:\n%2\n%3%4")
                .arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size()))
                .arg(QString::fromStdString(reconstruction_.txtPath))
                .arg(QString::fromStdString(reconstruction_.pcdPath))
                .arg(laserWarning));
    } catch (const WorkerException& ex) {
        Logger::instance().error("Reconstruction", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromStdString(ex.what()));
    } catch (const cv::Exception& ex) {
        Logger::instance().error("Reconstruction", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("OpenCV 重建异常：%1").arg(QString::fromStdString(ex.what())));
    } catch (const std::exception& ex) {
        Logger::instance().error("Reconstruction", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromStdString(ex.what()));
    } catch (...) {
        Logger::instance().error("Reconstruction", "Reconstruction failed with an unknown exception.");
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("重建时发生未知异常。"));
    }
}

void MainWindow::onCalibrationFrameCaptured()
{
    if (shuttingDown_.load()) return;
    try {
        calibrationCapture_ = calibrationFrameWatcher_.result();
        acquisition_ = calibrationCapture_;
        acquisitionPanel_->setCalibrationCaptureState(calibrationCaptureSessionService_.isActive(), calibrationCapture_.capturedFrameCount);
        acquisitionPanel_->setStatusText(QString::fromStdString(calibrationCapture_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定采集帧: %1").arg(calibrationCapture_.capturedFrameCount));
        setCaptureReviewResult(calibrationCapture_);
        setLiveStereoImages(calibrationCapture_.lastLeftPreview, calibrationCapture_.lastRightPreview);
        acquisitionPanel_->setCalibrationDirectories(calibrationCapture_.leftDirectory, calibrationCapture_.rightDirectory);

    } catch (const std::exception& ex) {
        acquisitionPanel_->setCalibrationCaptureState(calibrationCaptureSessionService_.isActive(), calibrationCapture_.capturedFrameCount);
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        Logger::instance().error("CalibrationCapture", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("采集当前帧失败"), QString::fromStdString(ex.what()));
    }
    setBusy(false, QString());
}

void MainWindow::onReconstructionCaptureFinished()
{
    if (shuttingDown_.load()) return;
    setBusy(false, QString());
    try {
        reconstructionCapture_ = reconstructionCaptureWatcher_.result();
        acquisition_ = reconstructionCapture_;
        acquisitionPanel_->setStatusText(QString::fromStdString(reconstructionCapture_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("重建采集帧: %1").arg(reconstructionCapture_.capturedFrameCount));
        setCaptureReviewResult(reconstructionCapture_);
        setLiveStereoImages(reconstructionCapture_.lastLeftPreview, reconstructionCapture_.lastRightPreview);
        if (reconstructionCapture_.success) {
            acquisitionPanel_->setReconstructionDirectories(reconstructionCapture_.leftDirectory, reconstructionCapture_.rightDirectory);

            QMessageBox::information(
                this,
                QString::fromUtf8("重建采集完成"),
                QString::fromUtf8("重建采集完成，成功帧数: %1").arg(reconstructionCapture_.capturedFrameCount));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("重建采集未完整完成"), QString::fromStdString(reconstructionCapture_.message));
        }
    } catch (const std::exception& ex) {
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        Logger::instance().error("ReconstructionCapture", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建采集失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::setCaptureReviewResult(const AcquisitionSessionResult& result)
{
    if (captureReviewWidget_) {
        captureReviewWidget_->setCaptureResult(result);
    }
}

void MainWindow::onCaptureReviewResultChanged()
{
    if (!captureReviewWidget_) {
        return;
    }

    const AcquisitionSessionResult result = captureReviewWidget_->captureResult();
    acquisition_ = result;

    if (!calibrationCapture_.sessionDirectory.empty() && result.sessionDirectory == calibrationCapture_.sessionDirectory) {
        calibrationCapture_ = result;
        calibrationCaptureSessionService_.setCurrentResult(result);
        acquisitionPanel_->setCalibrationCaptureState(calibrationCaptureSessionService_.isActive(), calibrationCapture_.capturedFrameCount);
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定采集帧: %1").arg(calibrationCapture_.capturedFrameCount));
    }

    if (!reconstructionCapture_.sessionDirectory.empty() && result.sessionDirectory == reconstructionCapture_.sessionDirectory) {
        reconstructionCapture_ = result;
        acquisitionPanel_->setResultSummary(QString::fromUtf8("重建采集帧: %1").arg(reconstructionCapture_.capturedFrameCount));
    }



    acquisitionPanel_->setStatusText(QString::fromUtf8("已删除采集帧，当前剩余 %1 帧。").arg(result.capturedFrameCount));
}

QString MainWindow::calibrationResultsDirectory() const
{
    StereoCameraConfig cameraConfig;
    if (acquisitionPanel_) {
        cameraConfig = acquisitionPanel_->stereoCameraConfig();
    }

    QString root = QString::fromStdString(cameraConfig.outputDirectory);
    if (root.trimmed().isEmpty()) {
        root = QString::fromStdString(acquisitionPanel_->projectConfig().outputDirectory);
    }
    if (root.trimmed().isEmpty()) {
        root = ".";
    }

    return QDir(root).filePath("calibration/result");
}

QString MainWindow::defaultCalibrationFilePath() const
{
    const QString resultDirectory = calibrationResultsDirectory();
    QDir().mkpath(resultDirectory);
    const QString fileName = "stereo_calibration_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".yml";
    return QDir(resultDirectory).filePath(fileName);
}

} // namespace htmsr::app
