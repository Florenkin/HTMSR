#include "app/ui/MainWindow.h"

#include "app/acquisition/GalvoController.h"
#include "app/acquisition/GalvoCaptureSupport.h"
#include "app/services/ReconstructionStorage.h"
#include "app/services/ResultExportService.h"
#if HTMSR_WITH_HIK_CAMERA
#include "app/acquisition/HikCameraDevice.h"
#endif
#include "app/ui/AcquisitionPanel.h"
#include "app/ui/CaptureReviewWidget.h"
#include "app/ui/ImageViewWidget.h"
#include "app/ui/LogPanel.h"
#include "app/ui/PointCloudViewWidget.h"
#include "app/ui/SerialCommandPackWidget.h"
#include "core/LaserExtractionService.h"
#include "core/Logger.h"

#include <opencv2/core.hpp>

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QException>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFuture>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

class WorkerException final : public QException {
public:
    explicit WorkerException(std::string message)
        : message_(std::move(message))
    {
    }

    void raise() const override
    {
        throw *this;
    }

    WorkerException* clone() const override
    {
        return new WorkerException(*this);
    }

    const char* what() const noexcept override
    {
        return message_.c_str();
    }

private:
    std::string message_;
};

template <typename Function>
auto runWorkerTask(const char* taskName, Function&& function) -> decltype(function())
{
    try {
        return function();
    } catch (const WorkerException&) {
        throw;
    } catch (const std::exception& ex) {
        throw WorkerException(ex.what());
    } catch (...) {
        throw WorkerException(std::string(taskName) + " failed with an unknown non-standard exception.");
    }
}

htmsr::ReconstructionResult failedReconstructionResult(const std::string& message)
{
    htmsr::ReconstructionResult result;
    result.success = false;
    result.message = message;
    return result;
}

htmsr::ReconstructionResult runReconstructionTask(const htmsr::ReconstructionInput& input,
    const std::string& outputDirectory, const std::string& captureSessionDirectory)
{
    try {
        htmsr::ReconstructionService service;
        auto result = service.reconstruct(input);
        htmsr::app::ReconstructionStorage::savePointClouds(outputDirectory, result, captureSessionDirectory);
        return result;
    } catch (const cv::Exception& ex) {
        return failedReconstructionResult(std::string("OpenCV 重建异常：") + ex.what());
    } catch (const std::exception& ex) {
        return failedReconstructionResult(ex.what());
    } catch (...) {
        return failedReconstructionResult("重建失败：发生未知异常。");
    }
}

} // namespace

namespace htmsr::app {

namespace {

CameraDeviceInfo findSelectedCameraDevice(const std::vector<CameraDeviceInfo>& devices, const std::string& deviceId)
{
    for (const auto& device : devices) {
        if (device.id == deviceId) {
            return device;
        }
    }
    throw std::runtime_error("Camera device was not found, id=" + deviceId);
}

QString directionText(GalvoScanDirection direction)
{
    return direction == GalvoScanDirection::Forward ? QString::fromUtf8("正向") : QString::fromUtf8("反向");
}

QString syncModeText(GalvoSyncMode mode)
{
    return mode == GalvoSyncMode::Sync ? QString::fromUtf8("振镜同步") : QString::fromUtf8("振镜异步");
}

std::vector<unsigned char> parseHexCommandText(const QString& commandText)
{
    QString normalized = commandText.trimmed();
    normalized.replace(",", " ");
    normalized.replace(";", " ");
    normalized.replace("\r", " ");
    normalized.replace("\n", " ");
    normalized.replace("\t", " ");

    if (normalized.isEmpty()) {
        throw std::runtime_error("相机指令不能为空。");
    }

    QStringList tokens = normalized.split(' ', Qt::SkipEmptyParts);
    if (tokens.size() == 1) {
        QString compact = tokens.front().trimmed();
        if (compact.startsWith("0x", Qt::CaseInsensitive)) {
            compact = compact.mid(2);
        }
        if (compact.size() > 2 && compact.size() % 2 == 0) {
            tokens.clear();
            for (int i = 0; i < compact.size(); i += 2) {
                tokens << compact.mid(i, 2);
            }
        }
    }

    std::vector<unsigned char> command;
    command.reserve(static_cast<size_t>(tokens.size()));
    for (const QString& token : tokens) {
        QString cleaned = token.trimmed();
        if (cleaned.startsWith("0x", Qt::CaseInsensitive)) {
            cleaned = cleaned.mid(2);
        }

        bool ok = false;
        const uint value = cleaned.toUInt(&ok, 16);
        if (!ok || value > 0xFFu) {
            throw std::runtime_error(QString::fromUtf8("相机指令包含非法字节：%1").arg(token).toStdString());
        }
        command.push_back(static_cast<unsigned char>(value));
    }

    if (command.empty()) {
        throw std::runtime_error("相机指令不能为空。");
    }
    return command;
}

QString bytesToHexText(const std::vector<unsigned char>& bytes)
{
    QStringList parts;
    for (unsigned char byte : bytes) {
        parts << QString("%1").arg(static_cast<unsigned int>(byte), 2, 16, QChar('0')).toUpper();
    }
    return parts.join(' ');
}

bool shouldRefreshPreviewAfterRawCommand(const std::vector<unsigned char>& command)
{
    if (command.size() < 4) {
        return false;
    }

    const unsigned char operation = command[3];
    return operation == 0x04 || operation == 0x05 || operation == 0x09 ||
        operation == 0x1A || operation == 0x1B;
}

GalvoMotionVerificationResult configureAndVerifyGalvoMotionParameters(const GalvoScanConfig& galvoConfig)
{
    SerialGalvoController controller(galvoConfig);
    try {
        const auto motion = configureAndVerifyGalvoMotion(controller, galvoConfig);
        controller.disconnect();
        return {
            true,
            QString::fromUtf8("振镜已连接，参数重新设置并验证通过：步进 %1°，自动旋转 %2°")
                .arg(motion.stepAngleDeg, 0, 'f', 4)
                .arg(motion.totalRotationAngleDeg),
            motion.stepAngleDeg,
            motion.totalRotationAngleDeg,
            QString::fromStdString(motion.portName)
        };
    } catch (const std::exception& ex) {
        controller.disconnect();
        return {false, QString::fromUtf8("振镜连接或参数回读异常：%1").arg(QString::fromUtf8(ex.what()))};
    } catch (...) {
        controller.disconnect();
        return {false, QString::fromUtf8("振镜连接或参数回读发生未知异常。")};
    }
}

} // namespace

/*
    函数功能：构造主窗口，初始化日志桥接、中央视图、Dock、菜单、工具栏和后台任务连接
    输入：
        parent：Qt 父控件
    输出：
        无（构造后主窗口进入可交互状态）
*/
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("HTMSR");
    resize(1440, 920);
    setStyleSheet(
        "QMainWindow { background: #d9d9d9; }"
        "QDockWidget::title { background: #d6d6d6; padding: 4px; border: 1px solid #b8b8b8; }"
        "QTabWidget::pane { border: 1px solid #b8b8b8; background: #f4f4f4; }"
        "QTabBar::tab { background: #e8e8e8; padding: 4px 12px; border: 1px solid #b8b8b8; }"
        "QTabBar::tab:selected { background: #ffffff; }"
        "QTableWidget, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #ffffff; border: 1px solid #c0c0c0; }"
        "QPushButton { padding: 3px 8px; }");

    logSink_ = new QtLogSink(this);
    connect(logSink_, &QtLogSink::messageReceived, this, [this](const LogMessage& message) {
        logPanel_->appendMessage(message);
    }, Qt::QueuedConnection);

    // 按参考界面布局依次构建中央视图和 Dock 区域。
    buildCentralView();
    buildDocks();

    progressBar_ = new QProgressBar;
    progressBar_->setMaximumWidth(180);
    progressBar_->setValue(0);
    galvoStatusLabel_ = new QLabel;
    galvoStatusLabel_->setMinimumWidth(520);
    statusBar()->addPermanentWidget(galvoStatusLabel_, 1);
    statusBar()->addPermanentWidget(progressBar_);

    acquisitionPanel_->setProjectConfig(configService_.load());
    refreshProjectTree();

    connect(&calibrationWatcher_, &QFutureWatcher<CalibrationResult>::finished, this, &MainWindow::onCalibrationFinished);
    connect(&reconstructionWatcher_, &QFutureWatcher<ReconstructionResult>::finished, this, &MainWindow::onReconstructionFinished);
    connect(&acquisitionWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onAcquisitionFinished);
    connect(&calibrationFrameWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onCalibrationFrameCaptured);
    connect(&reconstructionCaptureWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onReconstructionCaptureFinished);
    connect(&autoCalibrationWatcher_, &QFutureWatcher<IntegratedWorkflowResult>::finished, this, &MainWindow::onAutoCalibrationFinished);
    connect(&scanWorkflowWatcher_, &QFutureWatcher<IntegratedWorkflowResult>::finished, this, &MainWindow::onScanAndReconstructFinished);
    connect(&reconstructionGalvoPreflightWatcher_, &QFutureWatcher<GalvoMotionVerificationResult>::finished, this, &MainWindow::onReconstructionGalvoPreflightFinished);
    connect(&galvoMotionParametersWatcher_, &QFutureWatcher<GalvoMotionVerificationResult>::finished, this, &MainWindow::onGalvoMotionParametersApplied);
    connect(&serialCommandPackWatcher_, &QFutureWatcher<SerialCommandPackSendResult>::finished, this, &MainWindow::onSerialCommandPackFinished);
    connect(serialCommandPackWidget_, &SerialCommandPackWidget::sendRequested, this, &MainWindow::sendSerialCommandPack);
    connect(acquisitionPanel_, &AcquisitionPanel::refreshDevicesRequested, this, &MainWindow::refreshAcquisitionDevices);
    connect(acquisitionPanel_, &AcquisitionPanel::captureCalibrationFrameRequested, this, &MainWindow::captureCalibrationFrame);
    connect(acquisitionPanel_, &AcquisitionPanel::calibrateCapturedFramesRequested, this, &MainWindow::calibrateCapturedFrames);
    connect(acquisitionPanel_, &AcquisitionPanel::exportCalibrationRequested, this, &MainWindow::exportCalibration);
    connect(acquisitionPanel_, &AcquisitionPanel::calibrationFileSelected, this, &MainWindow::loadCalibrationFile);
    connect(acquisitionPanel_, &AcquisitionPanel::finishCalibrationCaptureRequested, this, &MainWindow::finishCalibrationCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::startReconstructionCaptureRequested, this, &MainWindow::startReconstructionCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::reconstructCapturedFramesRequested, this, &MainWindow::reconstructCapturedFrames);
    connect(acquisitionPanel_, &AcquisitionPanel::exportReconstructionRequested, this, &MainWindow::exportReconstruction);
    connect(acquisitionPanel_, &AcquisitionPanel::sendRawGalvoCommandRequested, this, &MainWindow::sendRawGalvoCommand);
    connect(acquisitionPanel_, &AcquisitionPanel::cameraConfigChanged, this, &MainWindow::restartLivePreview);
    connect(acquisitionPanel_, &AcquisitionPanel::galvoConfigChanged, this, [this]() {
        updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig());
    });
    connect(acquisitionPanel_, &AcquisitionPanel::galvoMotionParametersChanged, this, &MainWindow::applyGalvoMotionParameters);

    refreshAcquisitionDevices();
    startLivePreview();
    updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig(),
        acquisitionPanel_->integratedScanConfig().galvo.portName.empty()
            ? QString::fromUtf8("未识别到振镜串口") : QString::fromUtf8("串口已识别，通信待验证"));
    Logger::instance().info("App", "HTMSR started.");
}

// 析构时等待后台任务退出，避免窗口关闭后仍有线程访问已经销毁的 UI 对象。
MainWindow::~MainWindow()
{
    shuttingDown_.store(true);
    disconnect(logSink_, nullptr, this, nullptr);
    disconnect(&calibrationWatcher_, nullptr, this, nullptr);
    disconnect(&reconstructionWatcher_, nullptr, this, nullptr);
    disconnect(&acquisitionWatcher_, nullptr, this, nullptr);
    disconnect(&calibrationFrameWatcher_, nullptr, this, nullptr);
    disconnect(&reconstructionCaptureWatcher_, nullptr, this, nullptr);
    disconnect(&autoCalibrationWatcher_, nullptr, this, nullptr);
    disconnect(&scanWorkflowWatcher_, nullptr, this, nullptr);
    disconnect(&reconstructionGalvoPreflightWatcher_, nullptr, this, nullptr);
    disconnect(&livePreviewWatcher_, nullptr, this, nullptr);
    disconnect(&galvoMotionParametersWatcher_, nullptr, this, nullptr);
    disconnect(&serialCommandPackWatcher_, nullptr, this, nullptr);
    serialCommandPackStopRequested_->store(true);
    stopLivePreview();
    if (calibrationWatcher_.isRunning()) {
        calibrationWatcher_.cancel();
        calibrationWatcher_.waitForFinished();
    }
    if (reconstructionWatcher_.isRunning()) {
        reconstructionWatcher_.cancel();
        reconstructionWatcher_.waitForFinished();
    }
    if (acquisitionWatcher_.isRunning()) {
        acquisitionWatcher_.cancel();
        acquisitionWatcher_.waitForFinished();
    }
    if (calibrationFrameWatcher_.isRunning()) {
        calibrationFrameWatcher_.cancel();
        calibrationFrameWatcher_.waitForFinished();
    }
    if (reconstructionCaptureWatcher_.isRunning()) {
        reconstructionCaptureWatcher_.cancel();
        reconstructionCaptureWatcher_.waitForFinished();
    }
    if (reconstructionGalvoPreflightWatcher_.isRunning()) {
        reconstructionGalvoPreflightWatcher_.cancel();
        reconstructionGalvoPreflightWatcher_.waitForFinished();
    }
    if (autoCalibrationWatcher_.isRunning()) {
        autoCalibrationWatcher_.cancel();
        autoCalibrationWatcher_.waitForFinished();
    }
    if (scanWorkflowWatcher_.isRunning()) {
        scanWorkflowWatcher_.cancel();
        scanWorkflowWatcher_.waitForFinished();
    }
    if (galvoMotionParametersWatcher_.isRunning()) {
        galvoMotionParametersWatcher_.cancel();
        galvoMotionParametersWatcher_.waitForFinished();
    }
    if (serialCommandPackWatcher_.isRunning()) {
        serialCommandPackWatcher_.waitForFinished();
    }
    calibrationCaptureSessionService_.finish();
    QCoreApplication::removePostedEvents(this);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    shuttingDown_.store(true);
    acquisitionPanel_->commitPendingEdits();
    configService_.save(acquisitionPanel_->projectConfig());
    serialCommandPackStopRequested_->store(true);
    stopLivePreview();
    QCoreApplication::removePostedEvents(this);
    QMainWindow::closeEvent(event);
}

/*
    函数功能：从参数面板读取标定参数并在后台启动双目标定任务
    输入：
        无
    输出：
        无（函数会切换忙碌状态并启动异步标定）
*/
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
    calibrationWatcher_.setFuture(QtConcurrent::run([input]() {
        return runWorkerTask("Calibration", [input]() {
            CalibrationService service;
            return service.calibrate(input);
        });
    }));
}

/*
    函数功能：从磁盘加载已有的双目标定文件
    输入：
        无
    输出：
        无（加载成功后更新内存中的标定结果，失败时弹出提示）
*/
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

/*
    函数功能：枚举当前可用的在线采集设备，并刷新采集面板下拉框
    输入：
        无
    输出：
        无（失败时在日志和采集面板中反馈错误）
*/
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

/*
    函数功能：从采集面板读取配置并在后台启动在线采集任务
    输入：
        无
    输出：
        无（函数会切换忙碌状态并启动异步采集）
*/
void MainWindow::runAcquisition()
{
    if (acquisitionWatcher_.isRunning()) {
        return;
    }

    StereoCameraConfig config = acquisitionPanel_->stereoCameraConfig();
    if (config.outputDirectory.empty()) {
        config.outputDirectory = acquisitionPanel_->projectConfig().outputDirectory;
    }

    setBusy(true, QString::fromUtf8("采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("采集中..."));
    acquisitionWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Acquisition", [this, config]() {
            AcquisitionService service;
            return service.capture(config, [this](int, int, const FramePair& frame) {
                enqueueLivePreview(frame);
            });
        });
    }));
}

void MainWindow::runAutoCalibration()
{
    if (autoCalibrationWatcher_.isRunning()) {
        return;
    }

    IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    config.calibrationInput = acquisitionPanel_->calibrationInput();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = acquisitionPanel_->projectConfig().outputDirectory;
    }

    setBusy(true, QString::fromUtf8("自动标定中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("自动标定中..."));
    autoCalibrationWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Auto calibration", [this, config]() {
            return integratedCalibrationCaptureService_.run(config, [this](int, int, const FramePair& frame) {
                enqueueLivePreview(frame);
            });
        });
    }));
}

void MainWindow::runScanAndReconstruct()
{
    if (scanWorkflowWatcher_.isRunning()) {
        return;
    }

    IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    config.calibrationInput = acquisitionPanel_->calibrationInput();
    const auto projectConfig = acquisitionPanel_->projectConfig();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = projectConfig.outputDirectory;
    }

    const ReconstructionInput reconstructionInput = acquisitionPanel_->reconstructionInput(CalibrationResult{});
    config.laserConfig = reconstructionInput.laserConfig;
    config.reconstructionRange = reconstructionInput.imageRange;
    config.matchDistanceThreshold = reconstructionInput.matchDistanceThreshold;
    config.calibrationFile = projectConfig.calibrationFile.empty()
        ? config.calibrationInput.outputFile
        : projectConfig.calibrationFile;

    if (calibration_.isValid() && !config.calibrationFile.empty()) {
        calibrationService_.saveCalibration(config.calibrationFile, calibration_);
    }

    setBusy(true, QString::fromUtf8("扫描重建中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("扫描重建中..."));
    scanWorkflowWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Scan and reconstruct", [this, config]() {
            return integratedScanService_.runScanAndReconstruct(config, [this](int, int, const FramePair& frame) {
                enqueueLivePreview(frame);
            });
        });
    }));
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

/*
    函数功能：选择或输入标定文件路径后直接加载标定结果
    输入：
        file：用户选择的标定文件路径
    输出：
        无（加载成功后会更新当前标定结果和参数面板中的标定文件路径）
*/
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
    refreshProjectTree();
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
    reconstructionGalvoPreflightWatcher_.setFuture(QtConcurrent::run([galvoConfig = config.galvo]() {
        return configureAndVerifyGalvoMotionParameters(galvoConfig);
    }));
}

void MainWindow::onReconstructionGalvoPreflightFinished()
{
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
            });
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

        if (!calibration_.isValid()) {
            const auto file = acquisitionPanel_->projectConfig().calibrationFile;
            calibrationService_.loadCalibration(file, calibration_);
        }
        if (!calibration_.isValid()) {
            QMessageBox::warning(this, QString::fromUtf8("缺少标定"), QString::fromUtf8("请先完成标定或加载有效标定文件。"));
            return;
        }

        input.calibration = calibration_;
        const auto outputDirectory = acquisitionPanel_->projectConfig().outputDirectory;
        Logger::instance().info("Reconstruction", "重建图像目录：左=" + input.leftDirectory + "，右=" + input.rightDirectory);
        const std::string captureSessionDirectory = leftDirectory.absolutePath() == rightDirectory.absolutePath()
            ? leftDirectory.absolutePath().toStdString() : std::string{};
        setBusy(true, QString::fromUtf8("重建当前采集帧..."), true);
        acquisitionPanel_->setStatusText(QString::fromUtf8("正在重建当前采集帧..."));
        reconstructionWatcher_.setFuture(QtConcurrent::run(runReconstructionTask, input, outputDirectory,
            captureSessionDirectory));
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


void MainWindow::sendRawGalvoCommand(const QString& commandText)
{
    if (shuttingDown_.load() || busy_ || galvoMotionParametersWatcher_.isRunning()) {
        return;
    }
    const IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();

    std::vector<unsigned char> command;
    try {
        command = parseHexCommandText(commandText);
    } catch (const std::exception& ex) {
        serialCommandPackWidget_->setRawGalvoResponseText(QString::fromUtf8("指令格式错误"));
        QMessageBox::warning(this, QString::fromUtf8("相机指令格式错误"), QString::fromStdString(ex.what()));
        return;
    }

    SerialGalvoController controller(config.galvo);
    try {
        serialCommandPackWidget_->setRawGalvoResponseText(QString::fromUtf8("等待返回..."));
        if (!controller.connect()) {
            throw std::runtime_error(controller.lastError());
        }

        const auto result = controller.sendRawCommand(command, true);
        if (!result.response.empty()) {
            serialCommandPackWidget_->appendRawGalvoResponse(bytesToHexText(result.response));
        }
        if (result.success) {
            serialCommandPackWidget_->setRawGalvoResponseText(QString::fromUtf8("已收到返回，最新指令显示在最上方。"));
        } else if (result.message == "No response received before timeout.") {
            serialCommandPackWidget_->setRawGalvoResponseText(result.response.empty()
                ? QString::fromUtf8("无返回（等待 %1 ms 超时）").arg(config.galvo.commandTimeoutMs)
                : QString::fromUtf8("收到字节但未匹配到有效返回帧；原始字节已列在下方。"));
        } else {
            serialCommandPackWidget_->setRawGalvoResponseText(QString::fromStdString(result.message));
            throw std::runtime_error(result.message.empty() ? "Failed to send raw galvo command." : result.message);
        }

        controller.disconnect();

        if (command.size() >= 4 && command[3] == 0x1A) {
            galvoLaserEnabled_ = true;
        } else if (command.size() >= 4 && command[3] == 0x1B) {
            galvoLaserEnabled_ = false;
        }

        const QString hexText = bytesToHexText(command);
        const QString note = QString::fromUtf8("已发送相机指令：%1").arg(hexText);
        acquisitionPanel_->setStatusText(note);
        updateGalvoStatusBar(config, note);
        Logger::instance().info(
            "Galvo",
            "Raw command sent from UI: " + hexText.toStdString() +
                (result.response.empty() ? ", response=<none>" : ", response=" + bytesToHexText(result.response).toStdString()));

        if (shouldRefreshPreviewAfterRawCommand(command)) {
            const bool shouldResumeLivePreview = livePreviewWatcher_.isRunning() && !busy_;
            if (shouldResumeLivePreview) {
                stopLivePreview();
            }
            try {
                refreshLaserSwitchPreview(config);
            } catch (const std::exception& ex) {
                const std::string message = std::string("Raw command preview refresh failed: ") + ex.what();
                Logger::instance().warning("Acquisition", message);
                acquisitionPanel_->setStatusText(QString::fromStdString(message));
            }
            if (shouldResumeLivePreview && !shuttingDown_.load() && !busy_) {
                startLivePreview();
            }
        }
    } catch (const std::exception& ex) {
        controller.disconnect();
        serialCommandPackWidget_->setRawGalvoResponseText(QString::fromStdString(ex.what()));
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        updateGalvoStatusBar(config, QString::fromStdString(ex.what()));
        Logger::instance().error("Galvo", ex.what());
        QMessageBox::warning(this, QString::fromUtf8("发送相机指令失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::sendSerialCommandPack(const QString& name, const QString& content)
{
    if (shuttingDown_.load() || busy_ || serialCommandPackWatcher_.isRunning() || galvoMotionParametersWatcher_.isRunning()) {
        serialCommandPackWidget_->setStatusText(QString::fromUtf8("串口正在被其他任务使用，请稍后再发送。"));
        return;
    }

    std::vector<SerialCommandPackStep> steps;
    try {
        steps = parseSerialCommandPack(content);
    } catch (const std::exception& ex) {
        serialCommandPackWidget_->setStatusText(QString::fromStdString(ex.what()));
        return;
    }

    const GalvoScanConfig galvoConfig = acquisitionPanel_->integratedScanConfig().galvo;
    const auto stopRequested = serialCommandPackStopRequested_;
    stopRequested->store(false);
    serialCommandPackWidget_->setStatusText(QString::fromUtf8("正在发送 %1，串口 %2；逐条结果见底部日志。")
        .arg(name, QString::fromStdString(galvoConfig.portName)));
    setBusy(true, QString::fromUtf8("串口命令包发送中..."));

    serialCommandPackWatcher_.setFuture(QtConcurrent::run(
        [name, steps = std::move(steps), galvoConfig, stopRequested]() -> SerialCommandPackSendResult {
            SerialCommandPackSendResult summary;
            SerialGalvoController controller(galvoConfig);
            const std::string context = "命令包 [" + name.toStdString() + "]";
            Logger::instance().info("SerialPack", context + " 开始发送，串口=" + galvoConfig.portName);
            if (!controller.connect()) {
                summary.message = QString::fromUtf8("%1：%2")
                    .arg(name, QString::fromStdString(controller.lastError()));
                Logger::instance().error("SerialPack", summary.message.toStdString());
                return summary;
            }

            summary.success = true;
            for (const auto& step : steps) {
                if (stopRequested->load()) {
                    summary.success = false;
                    summary.message = QString::fromUtf8("%1：窗口关闭，发送已中止。").arg(name);
                    break;
                }
                if (step.type == SerialCommandPackStep::Type::Wait) {
                    Logger::instance().debug("SerialPack", context + " 第" + std::to_string(step.lineNumber) +
                        "行：等待 " + std::to_string(step.waitMs) + " ms");
                    int remaining = step.waitMs;
                    while (remaining > 0 && !stopRequested->load()) {
                        const int slice = std::min(remaining, 50);
                        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
                        remaining -= slice;
                    }
                    continue;
                }

                const QString hex = bytesToHexText(step.command);
                const std::string stepContext = context + " 第" + std::to_string(step.lineNumber) +
                    "行：" + hex.toStdString();
                Logger::instance().debug("SerialPack", stepContext + " 准备发送" +
                    (step.expectResponse ? "（等待返回）" : "（不等待返回）"));
                const auto result = controller.sendRawCommand(step.command, step.expectResponse);
                if (!result.response.empty()) {
                    summary.receivedCommands.append(bytesToHexText(result.response));
                }
                if (!result.success) {
                    summary.success = false;
                    const QString detail = result.response.empty()
                        ? QString::fromStdString(result.message)
                        : QString::fromUtf8("收到字节但未匹配到有效返回帧：%1")
                            .arg(bytesToHexText(result.response));
                    summary.message = QString::fromUtf8("%1 第 %2 行发送失败：%3")
                        .arg(name).arg(step.lineNumber).arg(detail);
                    Logger::instance().error("SerialPack", stepContext + " 失败：" + result.message);
                    break;
                }

                if (step.command.size() == 5 && step.command[0] == 0x55 && step.command[1] == 0xAA &&
                    step.command[2] == 0x01 && step.command[4] == step.command[3]) {
                    if (step.command[3] == 0x1A || step.command[3] == 0x1B) {
                        summary.laserStateChanged = true;
                        summary.laserEnabled = step.command[3] == 0x1A;
                    }
                }
                Logger::instance().debug("SerialPack", stepContext +
                    (step.expectResponse
                        ? " 已收到返回：" + bytesToHexText(result.response).toStdString()
                        : " 串口写入成功；设备动作未确认"));
            }

            if (summary.success && stopRequested->load()) {
                summary.success = false;
                summary.message = QString::fromUtf8("%1：窗口关闭，发送已中止。").arg(name);
            }

            if (!summary.success && summary.laserStateChanged && summary.laserEnabled) {
                const auto offResult = controller.laserOff();
                if (offResult.success) {
                    summary.laserEnabled = false;
                    Logger::instance().warning("SerialPack", context + " 中止后已发送激光关闭命令。");
                } else {
                    summary.message += QString::fromUtf8(" 激光关闭命令发送失败，请检查设备状态。 ");
                    Logger::instance().error("SerialPack", context + " 中止后激光关闭命令发送失败：" + offResult.message);
                }
            }
            controller.disconnect();
            if (summary.success) {
                summary.message = QString::fromUtf8("%1：命令包发送完成；无返回命令只确认串口写入。").arg(name);
                Logger::instance().info("SerialPack", summary.message.toStdString());
            } else {
                Logger::instance().error("SerialPack", summary.message.toStdString());
            }
            return summary;
        }));
}

void MainWindow::onSerialCommandPackFinished()
{
    SerialCommandPackSendResult summary;
    try {
        summary = serialCommandPackWatcher_.result();
    } catch (const std::exception& ex) {
        summary.message = QString::fromStdString(ex.what());
        Logger::instance().error("SerialPack", summary.message.toStdString());
    }
    if (summary.laserStateChanged) {
        galvoLaserEnabled_ = summary.laserEnabled;
    }
    if (shuttingDown_.load()) {
        return;
    }
    for (const QString& response : summary.receivedCommands) {
        serialCommandPackWidget_->appendRawGalvoResponse(response);
    }
    setBusy(false, QString());
    serialCommandPackWidget_->setStatusText(summary.message);
    statusBar()->showMessage(summary.message);
    updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig(), summary.message);
    if (!summary.success) {
        QMessageBox::warning(this, QString::fromUtf8("命令包发送失败"), summary.message);
    }
}

void MainWindow::applyGalvoMotionParameters()
{
    galvoMotionParametersPending_ = true;
    if (shuttingDown_.load() || busy_ || galvoMotionParametersWatcher_.isRunning()) {
        return;
    }

    galvoMotionParametersPending_ = false;
    const GalvoScanConfig galvoConfig = acquisitionPanel_->integratedScanConfig().galvo;
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在向振镜下发运动参数..."));
    updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig(), QString::fromUtf8("正在下发运动参数"));

    galvoMotionParametersWatcher_.setFuture(QtConcurrent::run([galvoConfig]() -> GalvoMotionVerificationResult {
        SerialGalvoController controller(galvoConfig);
        if (!controller.connect()) {
            return { false, QString::fromStdString(controller.lastError()) };
        }

        const auto checkSetResult = [](const GalvoCommandResult& result, const char* action) {
            if (!result.success) {
                throw std::runtime_error(std::string("振镜参数设置失败：") + action + ". " + result.message);
            }
        };

        try {
            checkSetResult(controller.setStepAngle(galvoConfig.stepAngleDeg), "步进角度");
            checkSetResult(controller.setAutoRotationAngle(galvoConfig.autoRotationAngleDeg), "总旋转角度");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // Match the reliable manual-query path: reopen the port after the
            // write-only commands so any device-side acknowledgement or
            // parser state cannot affect the following readback.
            const auto reopenController = [&controller]() {
                controller.disconnect();
                if (!controller.connect()) {
                    throw std::runtime_error(controller.lastError());
                }
            };
            reopenController();

            double actualStepAngle = 0.0;
            GalvoCommandResult stepQueryResult;
            for (int attempt = 0; attempt < 3; ++attempt) {
                stepQueryResult = controller.getStepAngle(actualStepAngle);
                if (stepQueryResult.success) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
            if (!stepQueryResult.success) {
                throw std::runtime_error(
                    std::string("步进角度设置命令已发送，但回读验证失败：") + stepQueryResult.message);
            }

            reopenController();
            int actualAutoRotationAngle = 0;
            GalvoCommandResult autoAngleQueryResult;
            for (int attempt = 0; attempt < 3; ++attempt) {
                autoAngleQueryResult = controller.getAutoRotationAngle(actualAutoRotationAngle);
                if (autoAngleQueryResult.success) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
            if (!autoAngleQueryResult.success) {
                throw std::runtime_error(
                    std::string("总旋转角度设置命令已发送，但回读验证失败：") + autoAngleQueryResult.message);
            }

            constexpr double kStepAngleToleranceDeg = 0.006;
            if (std::abs(actualStepAngle - galvoConfig.stepAngleDeg) > kStepAngleToleranceDeg) {
                throw std::runtime_error(
                    "步进角度回读不一致，设置=" + std::to_string(galvoConfig.stepAngleDeg) +
                    "，实际=" + std::to_string(actualStepAngle));
            }
            if (actualAutoRotationAngle != galvoConfig.autoRotationAngleDeg) {
                throw std::runtime_error(
                    "总旋转角度回读不一致，设置=" + std::to_string(galvoConfig.autoRotationAngleDeg) +
                    "，实际=" + std::to_string(actualAutoRotationAngle));
            }

            controller.disconnect();
            return {
                true,
                QString::fromUtf8("振镜参数已下发并验证：步进 %1°，总角 %2°")
                    .arg(actualStepAngle, 0, 'f', 4)
                    .arg(actualAutoRotationAngle),
                actualStepAngle,
                actualAutoRotationAngle,
                QString::fromStdString(galvoConfig.portName)
            };
        } catch (const std::exception& ex) {
            controller.disconnect();
            return { false, QString::fromUtf8(ex.what()) };
        } catch (...) {
            controller.disconnect();
            return { false, QString::fromUtf8("振镜参数下发失败：未知异常") };
        }
    }));
}

void MainWindow::onGalvoMotionParametersApplied()
{
    const auto result = galvoMotionParametersWatcher_.result();
    const auto config = acquisitionPanel_->integratedScanConfig();
    if (result.success) {
        // 界面在后台下发期间仍可修改，仅在回读匹配当前设置时更新只读帧数。
        if (!galvoMotionParametersPending_ &&
            result.portName == QString::fromStdString(config.galvo.portName) &&
            std::abs(result.actualStepAngleDeg - config.galvo.stepAngleDeg) <= 0.006 &&
            result.actualTotalAngleDeg == config.galvo.autoRotationAngleDeg) {
            acquisitionPanel_->setFrameCountFromDevice(result.actualStepAngleDeg, result.actualTotalAngleDeg);
        }
        acquisitionPanel_->setStatusText(result.message);
        updateGalvoStatusBar(config, result.message);
        Logger::instance().info("Galvo", result.message.toStdString());
    } else {
        acquisitionPanel_->setStatusText(result.message);
        updateGalvoStatusBar(config, result.message);
        Logger::instance().error("Galvo", result.message.toStdString());
    }

    if (galvoMotionParametersPending_ && !shuttingDown_.load() && !busy_) {
        applyGalvoMotionParameters();
    }
}

void MainWindow::updateGalvoStatusBar(const IntegratedScanConfig& config, const QString& note)
{
    if (!galvoStatusLabel_) {
        return;
    }

    const QString laserState = galvoLaserEnabled_ ? QString::fromUtf8("开") : QString::fromUtf8("关");
    const bool cameraUsesHardwareTrigger = config.stereoCamera.leftParameters.useHardwareTrigger ||
        config.stereoCamera.rightParameters.useHardwareTrigger;
    const QString cameraMode = cameraUsesHardwareTrigger ? QString::fromUtf8("相机硬触发") : QString::fromUtf8("相机软件抓图");
    QString text = QString::fromUtf8("振镜 %1 | %2 | %3 | 步进 %4° | 总角 %5° | 自动 %6° | 抓图 %7ms | 等待 %8ms | 占空比 %9 | 电压 %10V | 激光 %11")
        .arg(QString::fromStdString(config.galvo.portName))
        .arg(syncModeText(config.galvo.syncMode) + QStringLiteral(" / ") + cameraMode)
        .arg(directionText(config.galvo.direction))
        .arg(config.galvo.stepAngleDeg, 0, 'f', 4)
        .arg(config.totalRotationAngleDeg, 0, 'f', 2)
        .arg(config.galvo.autoRotationAngleDeg)
        .arg(config.galvo.captureIntervalMs)
        .arg(config.galvo.continuousCaptureWaitMs)
        .arg(config.galvo.laserDuty)
        .arg(config.galvo.voltageRangeV, 0, 'f', 1)
        .arg(laserState);

    if (!note.isEmpty()) {
        text += QString::fromUtf8(" | ") + note;
    }
    galvoStatusLabel_->setText(text);
}

void MainWindow::onCalibrationFinished()
{
    setBusy(false, QString());
    try {
        calibration_ = calibrationWatcher_.result();
        updateResultAvailability();
        acquisitionPanel_->setCalibrationDirectories(pendingCalibrationInput_.leftDirectory, pendingCalibrationInput_.rightDirectory);
        acquisitionPanel_->setCalibrationFile(pendingCalibrationInput_.outputFile);
        configService_.save(acquisitionPanel_->projectConfig());
        acquisitionPanel_->setResultSummary(QString::fromUtf8("RMS: %1 | 标定帧: %2")
            .arg(calibration_.rms).arg(calibration_.successfulPairs));
        refreshProjectTree();
        QMessageBox::information(this, QString::fromUtf8("标定完成"),
            QString::fromUtf8("双目标定完成。RMS: %1\n标定结果已自动保存到：\n%2")
                .arg(calibration_.rms).arg(QString::fromStdString(pendingCalibrationInput_.outputFile)));
    } catch (const std::exception& ex) {
        Logger::instance().error("Calibration", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("标定失败"), QString::fromStdString(ex.what()));
    }
}

/*
    函数功能：处理后台重建任务完成后的 UI 更新
    输入：
        无
    输出：
        无（函数会刷新点云视图和任务状态）
*/
void MainWindow::onReconstructionFinished()
{
    setBusy(false, QString());
    try {
        auto completed = reconstructionWatcher_.result();
        if (!completed.success || completed.mergedPoints.empty()) {
            const QString message = completed.message.empty()
                ? QString::fromUtf8("没有重建出有效点云，请检查标定文件、ROI、激光阈值和采集图像。")
                : QString::fromStdString(completed.message);
            Logger::instance().warning("Reconstruction", message.toStdString());
            QMessageBox::warning(this, QString::fromUtf8("重建失败"), message);
            return;
        }
        reconstruction_ = std::move(completed);
        updateResultAvailability();
        pointCloudView_->setPoints(reconstruction_.mergedPoints);
        refreshProjectTree();
        QMessageBox::information(this, QString::fromUtf8("重建完成"),
            QString::fromUtf8("点云数量: %1\n已自动保存:\n%2\n%3")
                .arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size()))
                .arg(QString::fromStdString(reconstruction_.txtPath))
                .arg(QString::fromStdString(reconstruction_.pcdPath)));
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

/*
    函数功能：处理后台在线采集任务完成后的 UI 更新
    输入：
        无
    输出：
        无（函数会更新预览图、采集状态，并在成功时把输出目录回填到重建输入）
*/
void MainWindow::onAcquisitionFinished()
{
    setBusy(false, QString());
    try {
        acquisition_ = acquisitionWatcher_.result();
        acquisitionPanel_->setStatusText(QString::fromStdString(acquisition_.message));
        setCaptureReviewResult(acquisition_);
        setLiveStereoImages(acquisition_.lastLeftPreview, acquisition_.lastRightPreview);
        if (acquisition_.success) {
            acquisitionPanel_->setReconstructionDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
            refreshProjectTree();
            QMessageBox::information(this, QString::fromUtf8("采集完成"), QString::fromUtf8("采集完成，成功帧数: %1").arg(acquisition_.capturedFrameCount));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("采集无有效帧"), QString::fromStdString(acquisition_.message));
        }
    } catch (const std::exception& ex) {
        Logger::instance().error("Acquisition", ex.what());
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        QMessageBox::critical(this, QString::fromUtf8("采集失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onCalibrationFrameCaptured()
{
    try {
        calibrationCapture_ = calibrationFrameWatcher_.result();
        acquisition_ = calibrationCapture_;
        acquisitionPanel_->setCalibrationCaptureState(calibrationCaptureSessionService_.isActive(), calibrationCapture_.capturedFrameCount);
        acquisitionPanel_->setStatusText(QString::fromStdString(calibrationCapture_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定采集帧: %1").arg(calibrationCapture_.capturedFrameCount));
        setCaptureReviewResult(calibrationCapture_);
        setLiveStereoImages(calibrationCapture_.lastLeftPreview, calibrationCapture_.lastRightPreview);
        acquisitionPanel_->setCalibrationDirectories(calibrationCapture_.leftDirectory, calibrationCapture_.rightDirectory);
        refreshProjectTree();
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
            refreshProjectTree();
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

void MainWindow::onAutoCalibrationFinished()
{
    setBusy(false, QString());
    try {
        autoCalibrationWorkflow_ = autoCalibrationWatcher_.result();
        acquisition_ = autoCalibrationWorkflow_.acquisition;
        acquisitionPanel_->setStatusText(QString::fromStdString(autoCalibrationWorkflow_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("RMS: %1").arg(autoCalibrationWorkflow_.calibration.rms));
        setCaptureReviewResult(acquisition_);
        setLiveStereoImages(acquisition_.lastLeftPreview, acquisition_.lastRightPreview);
        if (autoCalibrationWorkflow_.success) {
            calibration_ = autoCalibrationWorkflow_.calibration;
            updateResultAvailability();
            acquisitionPanel_->setCalibrationDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
            refreshProjectTree();
            QMessageBox::information(
                this,
                QString::fromUtf8("自动标定完成"),
                QString::fromUtf8("标定完成，RMS: %1").arg(calibration_.rms));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("自动标定失败"), QString::fromStdString(autoCalibrationWorkflow_.message));
        }
    } catch (const std::exception& ex) {
        Logger::instance().error("IntegratedWorkflow", ex.what());
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        QMessageBox::critical(this, QString::fromUtf8("自动标定失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onScanAndReconstructFinished()
{
    setBusy(false, QString());
    try {
        scanWorkflow_ = scanWorkflowWatcher_.result();
        acquisition_ = scanWorkflow_.acquisition;
        acquisitionPanel_->setStatusText(QString::fromStdString(scanWorkflow_.message));
        acquisitionPanel_->setResultSummary(
            QString::fromUtf8("点云: %1 | 复用标定: %2")
                .arg(static_cast<qulonglong>(scanWorkflow_.reconstruction.mergedPoints.size()))
                .arg(scanWorkflow_.usedExistingCalibration ? QString::fromUtf8("是") : QString::fromUtf8("否")));
        setCaptureReviewResult(acquisition_);
        setLiveStereoImages(acquisition_.lastLeftPreview, acquisition_.lastRightPreview);
        if (scanWorkflow_.success) {
            reconstruction_ = scanWorkflow_.reconstruction;
            calibration_ = scanWorkflow_.calibration;
            updateResultAvailability();
            pointCloudView_->setPoints(reconstruction_.mergedPoints);
            acquisitionPanel_->setReconstructionDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
            refreshProjectTree();
            QMessageBox::information(
                this,
                QString::fromUtf8("扫描重建完成"),
                QString::fromUtf8("点云数量: %1\n已自动保存:\n%2\n%3")
                    .arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size()))
                    .arg(QString::fromStdString(reconstruction_.txtPath))
                    .arg(QString::fromStdString(reconstruction_.pcdPath)));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("扫描重建失败"), QString::fromStdString(scanWorkflow_.message));
        }
    } catch (const std::exception& ex) {
        Logger::instance().error("IntegratedWorkflow", ex.what());
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        QMessageBox::critical(this, QString::fromUtf8("扫描重建失败"), QString::fromStdString(ex.what()));
    }
}

// Dock 区域保留右侧在线工作流和底部日志面板，主显示区留给图像与点云。
void MainWindow::buildDocks()
{
    acquisitionPanel_ = new AcquisitionPanel;
    serialCommandPackWidget_->setRawGalvoCommandWidget(acquisitionPanel_->rawGalvoCommandWidget());
    auto* acquisitionDock = new QDockWidget(QString::fromUtf8("在线工作流"), this);
    acquisitionDock->setWidget(acquisitionPanel_);
    addDockWidget(Qt::RightDockWidgetArea, acquisitionDock);

    // 默认保留两行可见空间，面板会自动滚动到最新记录。
    logPanel_ = new LogPanel;
    auto* bottomDock = new QDockWidget(QString::fromUtf8("消息"), this);
    bottomDock->setWidget(logPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
    QTimer::singleShot(0, this, [this, bottomDock]() {
        const int titleHeight = bottomDock->height() - logPanel_->height();
        resizeDocks({bottomDock}, {logPanel_->sizeHint().height() + titleHeight}, Qt::Vertical);
    });
}

// 中央区域以标签页形式承载双目实时预览、采集筛选和点云视图。
void MainWindow::buildCentralView()
{
    auto* tabs = new QTabWidget;
    auto* liveStereoPage = new QWidget;
    auto* liveStereoStackLayout = new QGridLayout(liveStereoPage);
    liveStereoStackLayout->setContentsMargins(0, 0, 0, 0);
    liveStereoStackLayout->setSpacing(0);

    auto* liveStereoContent = new QWidget;
    auto* liveStereoLayout = new QHBoxLayout(liveStereoContent);
    liveStereoLayout->setContentsMargins(0, 0, 0, 0);
    liveStereoLayout->setSpacing(0);

    auto* leftLivePane = new QWidget;
    auto* leftLiveLayout = new QVBoxLayout(leftLivePane);
    leftLiveLayout->setContentsMargins(0, 0, 0, 0);
    liveLeftImageView_ = new ImageViewWidget;
    leftLiveLayout->addWidget(liveLeftImageView_, 1);

    auto* rightLivePane = new QWidget;
    auto* rightLiveLayout = new QVBoxLayout(rightLivePane);
    rightLiveLayout->setContentsMargins(0, 0, 0, 0);
    liveRightImageView_ = new ImageViewWidget;
    rightLiveLayout->addWidget(liveRightImageView_, 1);

    auto* stereoDivider = new QFrame;
    stereoDivider->setFixedWidth(2);
    stereoDivider->setFrameShape(QFrame::NoFrame);
    stereoDivider->setStyleSheet("background: #000000;");

    liveStereoLayout->addWidget(leftLivePane, 1);
    liveStereoLayout->addWidget(stereoDivider);
    liveStereoLayout->addWidget(rightLivePane, 1);

    liveFpsLabel_ = new QLabel;
    liveFpsLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    liveFpsLabel_->setStyleSheet(
        "QLabel { background: rgba(0, 0, 0, 150); color: #ffffff; padding: 4px 8px; border-radius: 3px; }");
    liveStereoStackLayout->addWidget(liveStereoContent, 0, 0);
    liveStereoStackLayout->addWidget(liveFpsLabel_, 0, 0, Qt::AlignLeft | Qt::AlignBottom);
    liveFpsLabel_->raise();
    resetLiveFps();

    captureReviewWidget_ = new CaptureReviewWidget;
    connect(captureReviewWidget_, &CaptureReviewWidget::captureResultChanged, this, &MainWindow::onCaptureReviewResultChanged);
    serialCommandPackWidget_ = new SerialCommandPackWidget;

    pointCloudView_ = new PointCloudViewWidget;
    tabs->addTab(liveStereoPage, QString::fromUtf8("双目"));
    tabs->addTab(pointCloudView_, QString::fromUtf8("点云"));
    tabs->addTab(captureReviewWidget_, QString::fromUtf8("采集"));
    tabs->addTab(serialCommandPackWidget_, QString::fromUtf8("命令"));
    setCentralWidget(tabs);
}

void MainWindow::refreshProjectTree()
{
}

void MainWindow::updateResultAvailability()
{
    if (acquisitionPanel_) {
        acquisitionPanel_->setResultAvailability(calibration_.isValid(),
            reconstruction_.success && !reconstruction_.mergedPoints.empty());
    }
}

// 忙碌状态统一驱动底部进度条和在线采集面板按钮的可用性。
void MainWindow::setBusy(bool busy, const QString& text, bool keepLivePreview)
{
    busy_ = busy;
    updateResultAvailability();
    if (busy_ && !keepLivePreview) {
        stopLivePreview();
    }

    progressBar_->setRange(busy ? 0 : 0, busy ? 0 : 100);
    progressBar_->setValue(busy ? 0 : 100);
    statusBar()->showMessage(text);
    if (acquisitionPanel_) {
        acquisitionPanel_->setBusy(busy);
    }
    if (captureReviewWidget_) {
        captureReviewWidget_->setEnabled(!busy);
    }
    if (serialCommandPackWidget_) {
        serialCommandPackWidget_->setSendingAvailable(!busy);
    }

    if (!busy_) {
        if (shuttingDown_.load()) {
            return;
        }
        startLivePreview();
    }
}

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

    if (!autoCalibrationWorkflow_.acquisition.sessionDirectory.empty() &&
        result.sessionDirectory == autoCalibrationWorkflow_.acquisition.sessionDirectory) {
        autoCalibrationWorkflow_.acquisition = result;
    }

    if (!scanWorkflow_.acquisition.sessionDirectory.empty() &&
        result.sessionDirectory == scanWorkflow_.acquisition.sessionDirectory) {
        scanWorkflow_.acquisition = result;
    }

    acquisitionPanel_->setStatusText(QString::fromUtf8("已删除采集帧，当前剩余 %1 帧。").arg(result.capturedFrameCount));
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
