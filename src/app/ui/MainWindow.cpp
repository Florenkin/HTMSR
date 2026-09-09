#include "app/ui/MainWindow.h"

#include "app/acquisition/GalvoController.h"
#if HTMSR_WITH_HIK_CAMERA
#include "app/acquisition/HikCameraDevice.h"
#endif
#include "app/ui/AcquisitionPanel.h"
#include "app/ui/ImageViewWidget.h"
#include "app/ui/LogPanel.h"
#include "app/ui/PointCloudViewWidget.h"
#include "core/LaserExtractionService.h"
#include "core/Logger.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QException>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFuture>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>
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

GalvoScanDirection oppositeDirection(GalvoScanDirection direction)
{
    return direction == GalvoScanDirection::Forward ? GalvoScanDirection::Reverse : GalvoScanDirection::Forward;
}

QString directionText(GalvoScanDirection direction)
{
    return direction == GalvoScanDirection::Forward ? QString::fromUtf8("正向") : QString::fromUtf8("反向");
}

QString syncModeText(GalvoSyncMode mode)
{
    return mode == GalvoSyncMode::Sync ? QString::fromUtf8("同步") : QString::fromUtf8("异步");
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
        "QMenuBar, QToolBar { background: #efefef; border-bottom: 1px solid #b8b8b8; }"
        "QDockWidget::title { background: #d6d6d6; padding: 4px; border: 1px solid #b8b8b8; }"
        "QTabWidget::pane { border: 1px solid #b8b8b8; background: #f4f4f4; }"
        "QTabBar::tab { background: #e8e8e8; padding: 4px 12px; border: 1px solid #b8b8b8; }"
        "QTabBar::tab:selected { background: #ffffff; }"
        "QTreeWidget, QTableWidget, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #ffffff; border: 1px solid #c0c0c0; }"
        "QPushButton { padding: 3px 8px; }");

    logSink_ = new QtLogSink(this);
    connect(logSink_, &QtLogSink::messageReceived, this, [this](const LogMessage& message) {
        logPanel_->appendMessage(message);
    }, Qt::QueuedConnection);

    // 按参考界面布局依次构建中央视图、Dock 区域、菜单和工具栏。
    buildCentralView();
    buildDocks();
    buildMenus();
    buildToolBar();

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
    connect(&calibrationCaptureStartWatcher_, &QFutureWatcher<CalibrationCaptureSessionState>::finished, this, &MainWindow::onCalibrationCaptureStarted);
    connect(&calibrationFrameWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onCalibrationFrameCaptured);
    connect(&reconstructionCaptureWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onReconstructionCaptureFinished);
    connect(&autoCalibrationWatcher_, &QFutureWatcher<IntegratedWorkflowResult>::finished, this, &MainWindow::onAutoCalibrationFinished);
    connect(&scanWorkflowWatcher_, &QFutureWatcher<IntegratedWorkflowResult>::finished, this, &MainWindow::onScanAndReconstructFinished);
    connect(acquisitionPanel_, &AcquisitionPanel::refreshDevicesRequested, this, &MainWindow::refreshAcquisitionDevices);
    connect(acquisitionPanel_, &AcquisitionPanel::startCalibrationCaptureRequested, this, &MainWindow::startCalibrationCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::captureCalibrationFrameRequested, this, &MainWindow::captureCalibrationFrame);
    connect(acquisitionPanel_, &AcquisitionPanel::calibrateCapturedFramesRequested, this, &MainWindow::calibrateCapturedFrames);
    connect(acquisitionPanel_, &AcquisitionPanel::saveCalibrationResultRequested, this, &MainWindow::saveCalibrationResult);
    connect(acquisitionPanel_, &AcquisitionPanel::loadCalibrationResultRequested, this, &MainWindow::loadCalibrationResult);
    connect(acquisitionPanel_, &AcquisitionPanel::finishCalibrationCaptureRequested, this, &MainWindow::finishCalibrationCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::startReconstructionCaptureRequested, this, &MainWindow::startReconstructionCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::reconstructCapturedFramesRequested, this, &MainWindow::reconstructCapturedFrames);
    connect(acquisitionPanel_, &AcquisitionPanel::returnGalvoCenterRequested, this, &MainWindow::returnGalvoToCenter);
    connect(acquisitionPanel_, &AcquisitionPanel::sendRawGalvoCommandRequested, this, &MainWindow::sendRawGalvoCommand);
    connect(acquisitionPanel_, &AcquisitionPanel::galvoConfigChanged, this, [this]() {
        updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig());
    });

    refreshAcquisitionDevices();
    updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig(), QString::fromUtf8("未连接"));
    Logger::instance().info("App", "HTMSR started.");
}

// 析构时等待后台任务退出，避免窗口关闭后仍有线程访问已经销毁的 UI 对象。
MainWindow::~MainWindow()
{
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
    if (calibrationCaptureStartWatcher_.isRunning()) {
        calibrationCaptureStartWatcher_.cancel();
        calibrationCaptureStartWatcher_.waitForFinished();
    }
    if (calibrationFrameWatcher_.isRunning()) {
        calibrationFrameWatcher_.cancel();
        calibrationFrameWatcher_.waitForFinished();
    }
    if (reconstructionCaptureWatcher_.isRunning()) {
        reconstructionCaptureWatcher_.cancel();
        reconstructionCaptureWatcher_.waitForFinished();
    }
    if (autoCalibrationWatcher_.isRunning()) {
        autoCalibrationWatcher_.cancel();
        autoCalibrationWatcher_.waitForFinished();
    }
    if (scanWorkflowWatcher_.isRunning()) {
        scanWorkflowWatcher_.cancel();
        scanWorkflowWatcher_.waitForFinished();
    }
    calibrationCaptureSessionService_.finish();
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
    if (calibrationWatcher_.isRunning()) {
        return;
    }

    const CalibrationInput input = acquisitionPanel_->calibrationInput();
    setBusy(true, QString::fromUtf8("标定中..."));
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

    CalibrationResult loaded;
    if (calibrationService_.loadCalibration(file.toStdString(), loaded)) {
        calibration_ = loaded;
        Logger::instance().info("App", "Calibration loaded from UI.");
    } else {
        QMessageBox::warning(this, QString::fromUtf8("加载失败"), QString::fromUtf8("标定文件无效或无法读取。"));
    }
}

/*
    函数功能：使用当前参数和标定结果在后台启动离线重建任务
    输入：
        无
    输出：
        无（函数会校验标定结果并启动异步重建）
*/
void MainWindow::runReconstruction()
{
    if (reconstructionWatcher_.isRunning()) {
        return;
    }

    if (!calibration_.isValid()) {
        const auto file = acquisitionPanel_->calibrationInput().outputFile;
        calibrationService_.loadCalibration(file, calibration_);
    }
    if (!calibration_.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("缺少标定"), QString::fromUtf8("请先完成标定或加载有效标定文件。"));
        return;
    }

    const ReconstructionInput input = acquisitionPanel_->reconstructionInput(calibration_);
    autoExportReconstructionOnFinish_ = false;
    setBusy(true, QString::fromUtf8("重建中..."));
    reconstructionWatcher_.setFuture(QtConcurrent::run([input]() {
        return runWorkerTask("Reconstruction", [input]() {
            ReconstructionService service;
            return service.reconstruct(input);
        });
    }));
}

/*
    函数功能：导出当前重建结果为 txt 点云文件
    输入：
        无
    输出：
        无（导出成功时写文件，失败时给出错误提示）
*/
void MainWindow::exportTxt()
{
    if (reconstruction_.mergedPoints.empty()) {
        QMessageBox::information(this, QString::fromUtf8("无点云"), QString::fromUtf8("当前没有可导出的点云。"));
        return;
    }

    const QString file = QFileDialog::getSaveFileName(this, QString::fromUtf8("导出 TXT"), outputPath("point_cloud.txt"), QString::fromUtf8("TXT (*.txt);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    try {
        pointCloudService_.saveTxt(file.toStdString(), reconstruction_.mergedPoints);
    } catch (const std::exception& ex) {
        Logger::instance().error("PointCloud", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("导出失败"), QString::fromStdString(ex.what()));
    }
}

/*
    函数功能：导出当前重建结果为 pcd 点云文件
    输入：
        无
    输出：
        无（导出成功时写文件，失败时给出错误提示）
*/
void MainWindow::exportPcd()
{
    if (reconstruction_.mergedPoints.empty()) {
        QMessageBox::information(this, QString::fromUtf8("无点云"), QString::fromUtf8("当前没有可导出的点云。"));
        return;
    }

    const QString file = QFileDialog::getSaveFileName(this, QString::fromUtf8("导出 PCD"), outputPath("point_cloud.pcd"), QString::fromUtf8("PCD (*.pcd);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    try {
        pointCloudService_.savePcd(file.toStdString(), reconstruction_.mergedPoints);
    } catch (const std::exception& ex) {
        Logger::instance().error("PointCloud", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("导出失败"), QString::fromStdString(ex.what()));
    }
}

// 将当前项目参数持久化到配置中，并同步刷新左侧资源树显示。
void MainWindow::saveProjectSettings()
{
    configService_.save(acquisitionPanel_->projectConfig());
    refreshProjectTree();
    Logger::instance().info("App", "Project settings saved.");
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
    try {
        const auto devices = acquisitionService_.enumerateDevices();
        acquisitionPanel_->setDevices(devices);
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
            return integratedCalibrationCaptureService_.run(config);
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
            return integratedScanService_.runScanAndReconstruct(config);
        });
    }));
}

void MainWindow::startCalibrationCapture()
{
    if (calibrationCaptureStartWatcher_.isRunning() || calibrationCaptureSessionService_.isActive()) {
        return;
    }

    StereoCameraConfig config = acquisitionPanel_->stereoCameraConfig();
    if (config.outputDirectory.empty()) {
        config.outputDirectory = acquisitionPanel_->projectConfig().outputDirectory;
    }

    setBusy(true, QString::fromUtf8("开始标定采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在连接相机并创建标定采集会话..."));
    calibrationCaptureStartWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Start calibration capture", [this, config]() {
            return calibrationCaptureSessionService_.start(config);
        });
    }));
}

void MainWindow::captureCalibrationFrame()
{
    if (calibrationFrameWatcher_.isRunning() || !calibrationCaptureSessionService_.isActive()) {
        return;
    }

    setBusy(true, QString::fromUtf8("采集当前标定帧..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在采集当前标定帧..."));
    calibrationFrameWatcher_.setFuture(QtConcurrent::run([this]() {
        return runWorkerTask("Capture calibration frame", [this]() {
            return calibrationCaptureSessionService_.captureCurrentFrame();
        });
    }));
}

void MainWindow::calibrateCapturedFrames()
{
    if (calibrationWatcher_.isRunning()) {
        return;
    }

    calibrationCapture_ = calibrationCaptureSessionService_.currentResult();
    if (calibrationCapture_.capturedFrameCount <= 0) {
        QMessageBox::warning(this, QString::fromUtf8("缺少标定帧"), QString::fromUtf8("请先采集至少一组左右标定图像。"));
        return;
    }
    if (calibrationCapture_.capturedFrameCount < 6) {
        const auto reply = QMessageBox::question(
            this,
            QString::fromUtf8("标定帧较少"),
            QString::fromUtf8("当前只采集了 %1 帧，标定精度可能不足。是否继续？").arg(calibrationCapture_.capturedFrameCount));
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    CalibrationInput input = acquisitionPanel_->calibrationInput();
    input.leftDirectory = calibrationCapture_.leftDirectory;
    input.rightDirectory = calibrationCapture_.rightDirectory;
    setBusy(true, QString::fromUtf8("标定当前采集帧..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在标定当前采集帧..."));
    calibrationWatcher_.setFuture(QtConcurrent::run([input]() {
        return runWorkerTask("Calibration", [input]() {
            CalibrationService service;
            return service.calibrate(input);
        });
    }));
}

/*
    函数功能：将当前内存中的双目标定结果保存到在线标定结果文件夹
    输入：
        无
    输出：
        无（函数会按时间戳生成 yml 文件，并把该文件路径回填为当前标定文件）
*/
void MainWindow::saveCalibrationResult()
{
    if (!calibration_.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("缺少标定结果"), QString::fromUtf8("请先完成一次有效标定，再保存标定结果。"));
        return;
    }

    const QString resultDirectory = calibrationResultsDirectory();
    if (!QDir().mkpath(resultDirectory)) {
        QMessageBox::critical(this, QString::fromUtf8("保存失败"), QString::fromUtf8("无法创建标定结果文件夹：%1").arg(resultDirectory));
        return;
    }

    const QString fileName = "stereo_calibration_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".yml";
    const QString filePath = QDir(resultDirectory).filePath(fileName);

    try {
        calibrationService_.saveCalibration(filePath.toStdString(), calibration_);
        acquisitionPanel_->setCalibrationFile(filePath.toStdString());
        configService_.save(acquisitionPanel_->projectConfig());
        acquisitionPanel_->setStatusText(QString::fromUtf8("标定结果已保存：%1").arg(filePath));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定文件: %1 | RMS: %2").arg(fileName).arg(calibration_.rms));
        refreshProjectTree();
        Logger::instance().info("Calibration", "Timestamped calibration result saved: " + filePath.toStdString());
        QMessageBox::information(this, QString::fromUtf8("保存完成"), QString::fromUtf8("标定结果已保存到：\n%1").arg(filePath));
    } catch (const std::exception& ex) {
        Logger::instance().error("Calibration", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("保存失败"), QString::fromStdString(ex.what()));
    }
}

/*
    函数功能：从在线标定结果文件夹加载历史双目标定文件
    输入：
        无
    输出：
        无（加载成功后会更新当前标定结果和参数面板中的标定文件路径）
*/
void MainWindow::loadCalibrationResult()
{
    const QString resultDirectory = calibrationResultsDirectory();
    QDir().mkpath(resultDirectory);
    const QString file = QFileDialog::getOpenFileName(
        this,
        QString::fromUtf8("加载标定结果"),
        resultDirectory,
        QString::fromUtf8("YAML (*.yml *.yaml);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    CalibrationResult loaded;
    if (!calibrationService_.loadCalibration(file.toStdString(), loaded)) {
        QMessageBox::warning(this, QString::fromUtf8("加载失败"), QString::fromUtf8("标定文件无效或无法读取。"));
        return;
    }

    calibration_ = loaded;
    acquisitionPanel_->setCalibrationFile(file.toStdString());
    configService_.save(acquisitionPanel_->projectConfig());
    acquisitionPanel_->setStatusText(QString::fromUtf8("已加载标定结果：%1").arg(file));
    acquisitionPanel_->setResultSummary(QString::fromUtf8("标定文件: %1 | RMS: %2").arg(QFileInfo(file).fileName()).arg(calibration_.rms));
    refreshProjectTree();
    Logger::instance().info("Calibration", "Calibration result loaded from result folder: " + file.toStdString());
    QMessageBox::information(this, QString::fromUtf8("加载完成"), QString::fromUtf8("已加载标定结果。RMS: %1").arg(calibration_.rms));
}

void MainWindow::finishCalibrationCapture()
{
    calibrationCaptureSessionService_.finish();
    calibrationCapture_ = calibrationCaptureSessionService_.currentResult();
    acquisitionPanel_->setCalibrationCaptureState(false, calibrationCapture_.capturedFrameCount);
    acquisitionPanel_->setStatusText(QString::fromUtf8("标定采集已结束，已采集 %1 帧。").arg(calibrationCapture_.capturedFrameCount));
}

void MainWindow::startReconstructionCapture()
{
    if (reconstructionCaptureWatcher_.isRunning()) {
        return;
    }

    IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    const auto projectConfig = acquisitionPanel_->projectConfig();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = projectConfig.outputDirectory;
    }

    setBusy(true, QString::fromUtf8("重建采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在采集重建图像序列..."));
    acquisitionPanel_->setReconstructionCaptureReady(false);
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
    if (reconstructionWatcher_.isRunning()) {
        return;
    }

    if (reconstructionCapture_.capturedFrameCount <= 0) {
        QMessageBox::warning(this, QString::fromUtf8("缺少重建帧"), QString::fromUtf8("请先执行开始重建采集。"));
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

    ReconstructionInput input = acquisitionPanel_->reconstructionInput(calibration_);
    input.leftDirectory = reconstructionCapture_.leftDirectory;
    input.rightDirectory = reconstructionCapture_.rightDirectory;
    autoExportReconstructionOnFinish_ = true;
    setBusy(true, QString::fromUtf8("重建当前采集帧..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在重建当前采集帧..."));
    reconstructionWatcher_.setFuture(QtConcurrent::run([input]() {
        return runWorkerTask("Reconstruction", [input]() {
            ReconstructionService service;
            return service.reconstruct(input);
        });
    }));
}

/*
    函数功能：处理后台标定任务完成后的 UI 更新
    输入：
        无
    输出：
        无（函数会恢复忙碌状态、保存标定结果并弹出提示）
*/
void MainWindow::returnGalvoToCenter()
{
    const IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    if (config.galvo.stepAngleDeg <= 0.0) {
        QMessageBox::warning(this, QString::fromUtf8("振镜回中心失败"), QString::fromUtf8("步进角度必须大于 0。"));
        return;
    }

    const int stepCount = static_cast<int>(std::lround((config.totalRotationAngleDeg * 0.5) / config.galvo.stepAngleDeg));
    if (stepCount <= 0) {
        QMessageBox::warning(this, QString::fromUtf8("振镜回中心失败"), QString::fromUtf8("按当前总旋转角度和步进角度计算得到的回中心步数为 0。"));
        return;
    }

    SerialGalvoController controller(config.galvo);
    const GalvoScanDirection returnDirection = oppositeDirection(config.galvo.direction);
    try {
        if (!controller.connect()) {
            throw std::runtime_error("Failed to open galvo serial port: " + config.galvo.portName);
        }

        const auto stepResult = controller.setStepAngle(config.galvo.stepAngleDeg);
        if (!stepResult.success) {
            throw std::runtime_error(stepResult.message.empty() ? "Failed to set galvo step angle." : stepResult.message);
        }

        for (int i = 0; i < stepCount; ++i) {
            const auto moveResult = controller.setScanDirection(returnDirection);
            if (!moveResult.success) {
                throw std::runtime_error(moveResult.message.empty() ? "Failed to move galvo toward center." : moveResult.message);
            }
        }

        controller.disconnect();
        const QString note = QString::fromUtf8("已按%1步向%2尝试回中心")
            .arg(stepCount)
            .arg(directionText(returnDirection));
        acquisitionPanel_->setStatusText(note);
        updateGalvoStatusBar(config, note);
        Logger::instance().info(
            "Galvo",
            "Galvo returned toward center by " + std::to_string(stepCount) +
                " steps, direction=" + toString(returnDirection));

        try {
            refreshLaserSwitchPreview(config);
        } catch (const std::exception& ex) {
            const std::string message = std::string("Galvo center preview refresh failed: ") + ex.what();
            Logger::instance().warning("Acquisition", message);
            acquisitionPanel_->setStatusText(QString::fromStdString(message));
        }
    } catch (const std::exception& ex) {
        controller.disconnect();
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        updateGalvoStatusBar(config, QString::fromStdString(ex.what()));
        Logger::instance().error("Galvo", ex.what());
        QMessageBox::warning(this, QString::fromUtf8("振镜回中心失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::sendRawGalvoCommand(const QString& commandText)
{
    const IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();

    std::vector<unsigned char> command;
    try {
        command = parseHexCommandText(commandText);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QString::fromUtf8("相机指令格式错误"), QString::fromStdString(ex.what()));
        return;
    }

    SerialGalvoController controller(config.galvo);
    try {
        if (!controller.connect()) {
            throw std::runtime_error("Failed to open galvo serial port: " + config.galvo.portName);
        }

        const auto result = controller.sendRawCommand(command, false);
        if (!result.success) {
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
        Logger::instance().info("Galvo", "Raw command sent from UI: " + hexText.toStdString());

        if (shouldRefreshPreviewAfterRawCommand(command)) {
            try {
                refreshLaserSwitchPreview(config);
            } catch (const std::exception& ex) {
                const std::string message = std::string("Raw command preview refresh failed: ") + ex.what();
                Logger::instance().warning("Acquisition", message);
                acquisitionPanel_->setStatusText(QString::fromStdString(message));
            }
        }
    } catch (const std::exception& ex) {
        controller.disconnect();
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        updateGalvoStatusBar(config, QString::fromStdString(ex.what()));
        Logger::instance().error("Galvo", ex.what());
        QMessageBox::warning(this, QString::fromUtf8("发送相机指令失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::updateGalvoStatusBar(const IntegratedScanConfig& config, const QString& note)
{
    if (!galvoStatusLabel_) {
        return;
    }

    const QString laserState = galvoLaserEnabled_ ? QString::fromUtf8("开") : QString::fromUtf8("关");
    QString text = QString::fromUtf8("振镜 %1 | %2 | %3 | 步进 %4° | 总角 %5° | 自动 %6° | 抓图 %7ms | 等待 %8ms | 占空比 %9 | 电压 %10V | 激光 %11")
        .arg(QString::fromStdString(config.galvo.portName))
        .arg(syncModeText(config.galvo.syncMode))
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
        if (!calibrationCapture_.leftDirectory.empty() && !calibrationCapture_.rightDirectory.empty()) {
            acquisitionPanel_->setCalibrationDirectories(calibrationCapture_.leftDirectory, calibrationCapture_.rightDirectory);
            acquisitionPanel_->setResultSummary(QString::fromUtf8("RMS: %1 | 标定帧: %2")
                .arg(calibration_.rms)
                .arg(calibrationCapture_.capturedFrameCount));
            refreshProjectTree();
        }
        QMessageBox::information(this, QString::fromUtf8("标定完成"), QString::fromUtf8("双目标定完成。RMS: %1").arg(calibration_.rms));
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
        无（函数会刷新点云视图、调试图和资源树）
*/
void MainWindow::onReconstructionFinished()
{
    setBusy(false, QString());
    try {
        reconstruction_ = reconstructionWatcher_.result();
        if (autoExportReconstructionOnFinish_ && !reconstruction_.mergedPoints.empty()) {
            reconstruction_.txtPath = outputPath("point_cloud.txt").toStdString();
            reconstruction_.pcdPath = outputPath("point_cloud.pcd").toStdString();
            pointCloudService_.saveTxt(reconstruction_.txtPath, reconstruction_.mergedPoints);
            pointCloudService_.savePcd(reconstruction_.pcdPath, reconstruction_.mergedPoints);
            autoExportReconstructionOnFinish_ = false;
        }
        pointCloudView_->setPoints(reconstruction_.mergedPoints);
        if (!reconstruction_.frames.empty()) {
            leftImageView_->setImage(reconstruction_.frames.front().leftLinePreview);
            rightImageView_->setImage(reconstruction_.frames.front().rightLinePreview);
            debugImageView_->setImage(reconstruction_.frames.front().leftLinePreview);
        }
        refreshProjectTree();
        QMessageBox::information(this, QString::fromUtf8("重建完成"), QString::fromUtf8("点云数量: %1").arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size())));
    } catch (const std::exception& ex) {
        autoExportReconstructionOnFinish_ = false;
        Logger::instance().error("Reconstruction", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromStdString(ex.what()));
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
        if (!acquisition_.lastLeftPreview.empty()) {
            leftImageView_->setImage(acquisition_.lastLeftPreview);
        }
        if (!acquisition_.lastRightPreview.empty()) {
            rightImageView_->setImage(acquisition_.lastRightPreview);
        }
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

void MainWindow::onCalibrationCaptureStarted()
{
    setBusy(false, QString());
    try {
        const auto state = calibrationCaptureStartWatcher_.result();
        calibrationCapture_ = state.acquisition;
        acquisition_ = calibrationCapture_;
        acquisitionPanel_->setCalibrationCaptureState(state.active, calibrationCapture_.capturedFrameCount);
        acquisitionPanel_->setStatusText(QString::fromStdString(calibrationCapture_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定采集帧: %1").arg(calibrationCapture_.capturedFrameCount));
        refreshProjectTree();
    } catch (const std::exception& ex) {
        calibrationCaptureSessionService_.finish();
        acquisitionPanel_->setCalibrationCaptureState(false, 0);
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        Logger::instance().error("CalibrationCapture", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("开始标定采集失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onCalibrationFrameCaptured()
{
    setBusy(false, QString());
    try {
        calibrationCapture_ = calibrationFrameWatcher_.result();
        acquisition_ = calibrationCapture_;
        acquisitionPanel_->setCalibrationCaptureState(calibrationCaptureSessionService_.isActive(), calibrationCapture_.capturedFrameCount);
        acquisitionPanel_->setStatusText(QString::fromStdString(calibrationCapture_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定采集帧: %1").arg(calibrationCapture_.capturedFrameCount));
        if (!calibrationCapture_.lastLeftPreview.empty()) {
            leftImageView_->setImage(calibrationCapture_.lastLeftPreview);
        }
        if (!calibrationCapture_.lastRightPreview.empty()) {
            rightImageView_->setImage(calibrationCapture_.lastRightPreview);
        }
        setLiveStereoImages(calibrationCapture_.lastLeftPreview, calibrationCapture_.lastRightPreview);
        acquisitionPanel_->setCalibrationDirectories(calibrationCapture_.leftDirectory, calibrationCapture_.rightDirectory);
        refreshProjectTree();
    } catch (const std::exception& ex) {
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        Logger::instance().error("CalibrationCapture", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("采集当前帧失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onReconstructionCaptureFinished()
{
    setBusy(false, QString());
    try {
        reconstructionCapture_ = reconstructionCaptureWatcher_.result();
        acquisition_ = reconstructionCapture_;
        acquisitionPanel_->setStatusText(QString::fromStdString(reconstructionCapture_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("重建采集帧: %1").arg(reconstructionCapture_.capturedFrameCount));
        acquisitionPanel_->setReconstructionCaptureReady(reconstructionCapture_.success);
        if (!reconstructionCapture_.lastLeftPreview.empty()) {
            leftImageView_->setImage(reconstructionCapture_.lastLeftPreview);
        }
        if (!reconstructionCapture_.lastRightPreview.empty()) {
            rightImageView_->setImage(reconstructionCapture_.lastRightPreview);
        }
        setLiveStereoImages(reconstructionCapture_.lastLeftPreview, reconstructionCapture_.lastRightPreview);
        if (reconstructionCapture_.success) {
            acquisitionPanel_->setReconstructionDirectories(reconstructionCapture_.leftDirectory, reconstructionCapture_.rightDirectory);
            refreshProjectTree();
            QMessageBox::information(
                this,
                QString::fromUtf8("重建采集完成"),
                QString::fromUtf8("重建采集完成，成功帧数: %1").arg(reconstructionCapture_.capturedFrameCount));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("重建采集无有效帧"), QString::fromStdString(reconstructionCapture_.message));
        }
    } catch (const std::exception& ex) {
        acquisitionPanel_->setReconstructionCaptureReady(false);
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
        if (!acquisition_.lastLeftPreview.empty()) {
            leftImageView_->setImage(acquisition_.lastLeftPreview);
        }
        if (!acquisition_.lastRightPreview.empty()) {
            rightImageView_->setImage(acquisition_.lastRightPreview);
        }
        setLiveStereoImages(acquisition_.lastLeftPreview, acquisition_.lastRightPreview);
        if (autoCalibrationWorkflow_.success) {
            calibration_ = autoCalibrationWorkflow_.calibration;
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
        if (!acquisition_.lastLeftPreview.empty()) {
            leftImageView_->setImage(acquisition_.lastLeftPreview);
        }
        if (!acquisition_.lastRightPreview.empty()) {
            rightImageView_->setImage(acquisition_.lastRightPreview);
        }
        setLiveStereoImages(acquisition_.lastLeftPreview, acquisition_.lastRightPreview);
        if (scanWorkflow_.success) {
            reconstruction_ = scanWorkflow_.reconstruction;
            calibration_ = scanWorkflow_.calibration;
            pointCloudView_->setPoints(reconstruction_.mergedPoints);
            if (!reconstruction_.frames.empty()) {
                debugImageView_->setImage(reconstruction_.frames.front().leftLinePreview);
            }
            acquisitionPanel_->setReconstructionDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
            refreshProjectTree();
            QMessageBox::information(
                this,
                QString::fromUtf8("扫描重建完成"),
                QString::fromUtf8("点云数量: %1").arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size())));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("扫描重建失败"), QString::fromStdString(scanWorkflow_.message));
        }
    } catch (const std::exception& ex) {
        Logger::instance().error("IntegratedWorkflow", ex.what());
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        QMessageBox::critical(this, QString::fromUtf8("扫描重建失败"), QString::fromStdString(ex.what()));
    }
}

// 顶部菜单栏负责暴露保存、采集、标定、重建和导出等主流程入口。
void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件"));
    fileMenu->addAction(QString::fromUtf8("保存项目"), this, &MainWindow::saveProjectSettings);
    fileMenu->addAction(QString::fromUtf8("加载标定"), this, &MainWindow::loadCalibration);
    fileMenu->addSeparator();
    fileMenu->addAction(QString::fromUtf8("退出"), qApp, &QApplication::quit);

    auto* scanMenu = menuBar()->addMenu(QString::fromUtf8("扫描"));
    scanMenu->addAction(QString::fromUtf8("刷新相机"), this, &MainWindow::refreshAcquisitionDevices);
    scanMenu->addSeparator();
    scanMenu->addAction(QString::fromUtf8("离线双目标定"), this, &MainWindow::runCalibration);
    scanMenu->addAction(QString::fromUtf8("离线三维重建"), this, &MainWindow::runReconstruction);

    auto* viewMenu = menuBar()->addMenu(QString::fromUtf8("显示"));
    viewMenu->addAction(QString::fromUtf8("清空点云"), pointCloudView_, &PointCloudViewWidget::clear);

    menuBar()->addMenu(QString::fromUtf8("设置"));
    menuBar()->addMenu(QString::fromUtf8("帮助"));
}

// 工具栏提供最常用的一组快捷操作，便于离线处理和在线采集快速切换。
void MainWindow::buildToolBar()
{
    auto* toolbar = addToolBar(QString::fromUtf8("工具"));
    toolbar->setMovable(false);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QString::fromUtf8("保存"), this, &MainWindow::saveProjectSettings);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), QString::fromUtf8("加载标定"), this, &MainWindow::loadCalibration);
    toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), QString::fromUtf8("刷新相机"), this, &MainWindow::refreshAcquisitionDevices);
    toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QString::fromUtf8("离线标定"), this, &MainWindow::runCalibration);
    toolbar->addAction(style()->standardIcon(QStyle::SP_ComputerIcon), QString::fromUtf8("离线重建"), this, &MainWindow::runReconstruction);
    toolbar->addSeparator();
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QString::fromUtf8("导出TXT"), this, &MainWindow::exportTxt);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DriveHDIcon), QString::fromUtf8("导出PCD"), this, &MainWindow::exportPcd);
}

// Dock 区域包括资源树、参数面板、采集面板和日志面板，构成主界面的工作区骨架。
void MainWindow::buildDocks()
{
    projectTree_ = new QTreeWidget;
    projectTree_->setHeaderLabel(QString::fromUtf8("项目"));

    auto* leftDock = new QDockWidget(QString::fromUtf8("资源"), this);
    leftDock->setWidget(projectTree_);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    acquisitionPanel_ = new AcquisitionPanel;
    auto* acquisitionDock = new QDockWidget(QString::fromUtf8("在线工作流"), this);
    acquisitionDock->setWidget(acquisitionPanel_);
    addDockWidget(Qt::RightDockWidgetArea, acquisitionDock);

    logPanel_ = new LogPanel;
    auto* bottomDock = new QDockWidget(QString::fromUtf8("消息"), this);
    bottomDock->setWidget(logPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
}

// 中央区域以标签页形式承载点云视图、左右图像和调试图像。
void MainWindow::buildCentralView()
{
    auto* tabs = new QTabWidget;
    auto* liveStereoPage = new QWidget;
    auto* liveStereoLayout = new QHBoxLayout(liveStereoPage);
    liveStereoLayout->setContentsMargins(0, 0, 0, 0);
    liveStereoLayout->setSpacing(6);

    auto* leftLivePane = new QWidget;
    auto* leftLiveLayout = new QVBoxLayout(leftLivePane);
    leftLiveLayout->setContentsMargins(0, 0, 0, 0);
    auto* leftLiveTitle = new QLabel(QString::fromUtf8("左相机实时图像"));
    leftLiveTitle->setAlignment(Qt::AlignCenter);
    liveLeftImageView_ = new ImageViewWidget;
    leftLiveLayout->addWidget(leftLiveTitle);
    leftLiveLayout->addWidget(liveLeftImageView_, 1);

    auto* rightLivePane = new QWidget;
    auto* rightLiveLayout = new QVBoxLayout(rightLivePane);
    rightLiveLayout->setContentsMargins(0, 0, 0, 0);
    auto* rightLiveTitle = new QLabel(QString::fromUtf8("右相机实时图像"));
    rightLiveTitle->setAlignment(Qt::AlignCenter);
    liveRightImageView_ = new ImageViewWidget;
    rightLiveLayout->addWidget(rightLiveTitle);
    rightLiveLayout->addWidget(liveRightImageView_, 1);

    liveStereoLayout->addWidget(leftLivePane, 1);
    liveStereoLayout->addWidget(rightLivePane, 1);

    pointCloudView_ = new PointCloudViewWidget;
    leftImageView_ = new ImageViewWidget;
    rightImageView_ = new ImageViewWidget;
    debugImageView_ = new ImageViewWidget;
    tabs->addTab(liveStereoPage, QString::fromUtf8("双目实时"));
    tabs->addTab(pointCloudView_, QString::fromUtf8("点云"));
    tabs->addTab(leftImageView_, QString::fromUtf8("左图"));
    tabs->addTab(rightImageView_, QString::fromUtf8("右图"));
    tabs->addTab(debugImageView_, QString::fromUtf8("调试图"));
    setCentralWidget(tabs);
}

// 左侧资源树只展示关键输入输出路径和统计信息，避免一次性展开大量图像文件。
void MainWindow::refreshProjectTree()
{
    if (!projectTree_) {
        return;
    }

    projectTree_->clear();
    const auto config = acquisitionPanel_ ? acquisitionPanel_->projectConfig() : AppProjectConfig{};
    auto* calibrationNode = new QTreeWidgetItem(projectTree_, { QString::fromUtf8("标定数据") });
    calibrationNode->addChild(new QTreeWidgetItem({ QString::fromStdString(config.leftCalibrationDirectory) }));
    calibrationNode->addChild(new QTreeWidgetItem({ QString::fromStdString(config.rightCalibrationDirectory) }));
    calibrationNode->addChild(new QTreeWidgetItem({ QString::fromUtf8("标定文件: %1").arg(QString::fromStdString(config.calibrationFile)) }));

    auto* reconstructionNode = new QTreeWidgetItem(projectTree_, { QString::fromUtf8("重建数据") });
    reconstructionNode->addChild(new QTreeWidgetItem({ QString::fromStdString(config.leftReconstructionDirectory) }));
    reconstructionNode->addChild(new QTreeWidgetItem({ QString::fromStdString(config.rightReconstructionDirectory) }));

    auto* acquisitionNode = new QTreeWidgetItem(projectTree_, { QString::fromUtf8("采集结果") });
    acquisitionNode->addChild(new QTreeWidgetItem({ QString::fromStdString(acquisition_.sessionDirectory) }));
    acquisitionNode->addChild(new QTreeWidgetItem({ QString::fromUtf8("成功帧数: %1").arg(acquisition_.capturedFrameCount) }));

    auto* resultNode = new QTreeWidgetItem(projectTree_, { QString::fromUtf8("结果点云") });
    resultNode->addChild(new QTreeWidgetItem({ QString::fromUtf8("点数: %1").arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size())) }));
    projectTree_->expandAll();
}

// 忙碌状态统一驱动底部进度条和在线采集面板按钮的可用性。
void MainWindow::setBusy(bool busy, const QString& text)
{
    progressBar_->setRange(busy ? 0 : 0, busy ? 0 : 100);
    progressBar_->setValue(busy ? 0 : 100);
    statusBar()->showMessage(text);
    if (acquisitionPanel_) {
        acquisitionPanel_->setBusy(busy);
    }
}

void MainWindow::enqueueLivePreview(const FramePair& frame)
{
    cv::Mat left = frame.left.clone();
    cv::Mat right = frame.right.clone();
    QMetaObject::invokeMethod(
        this,
        [this, left = std::move(left), right = std::move(right)]() mutable {
            setLiveStereoImages(left, right);
            if (!left.empty() && leftImageView_) {
                leftImageView_->setImage(left);
            }
            if (!right.empty() && rightImageView_) {
                rightImageView_->setImage(right);
            }
            if (!left.empty() && debugImageView_) {
                debugImageView_->setImage(left);
            }
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
        cv::Mat debug = extraction.preview.empty() ? frame.left.clone() : extraction.preview.clone();
        QMetaObject::invokeMethod(
            this,
            [this, left = std::move(left), right = std::move(right), debug = std::move(debug)]() mutable {
                setLiveStereoImages(left, right);
                if (!left.empty() && leftImageView_) {
                    leftImageView_->setImage(left);
                }
                if (!right.empty() && rightImageView_) {
                    rightImageView_->setImage(right);
                }
                if (!debug.empty() && debugImageView_) {
                    debugImageView_->setImage(debug);
                }
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

    return QDir(root).filePath("calibration_results");
}

// 导出路径默认基于当前项目输出目录拼接，减少每次导出时重复选路径。
QString MainWindow::outputPath(const QString& filename) const
{
    const auto config = acquisitionPanel_->projectConfig();
    return QDir(QString::fromStdString(config.outputDirectory)).filePath(filename);
}

} // namespace htmsr::app
