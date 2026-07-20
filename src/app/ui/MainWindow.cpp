#include "app/ui/MainWindow.h"

#include "app/ui/AcquisitionPanel.h"
#include "app/ui/ImageViewWidget.h"
#include "app/ui/LogPanel.h"
#include "app/ui/ParameterPanel.h"
#include "app/ui/PointCloudViewWidget.h"
#include "core/Logger.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFuture>
#include <QHeaderView>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QtConcurrent>

namespace htmsr::app {

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
    statusBar()->addPermanentWidget(progressBar_);

    parameterPanel_->setProjectConfig(configService_.load());
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
    connect(acquisitionPanel_, &AcquisitionPanel::finishCalibrationCaptureRequested, this, &MainWindow::finishCalibrationCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::startReconstructionCaptureRequested, this, &MainWindow::startReconstructionCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::reconstructCapturedFramesRequested, this, &MainWindow::reconstructCapturedFrames);

    refreshAcquisitionDevices();
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

    const CalibrationInput input = parameterPanel_->calibrationInput();
    setBusy(true, QString::fromUtf8("标定中..."));
    calibrationWatcher_.setFuture(QtConcurrent::run([input]() {
        CalibrationService service;
        return service.calibrate(input);
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
        const auto file = parameterPanel_->calibrationInput().outputFile;
        calibrationService_.loadCalibration(file, calibration_);
    }
    if (!calibration_.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("缺少标定"), QString::fromUtf8("请先完成标定或加载有效标定文件。"));
        return;
    }

    const ReconstructionInput input = parameterPanel_->reconstructionInput(calibration_);
    autoExportReconstructionOnFinish_ = false;
    setBusy(true, QString::fromUtf8("重建中..."));
    reconstructionWatcher_.setFuture(QtConcurrent::run([input]() {
        ReconstructionService service;
        return service.reconstruct(input);
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
    configService_.save(parameterPanel_->projectConfig());
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
        config.outputDirectory = parameterPanel_->projectConfig().outputDirectory;
    }

    setBusy(true, QString::fromUtf8("采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("采集中..."));
    acquisitionWatcher_.setFuture(QtConcurrent::run([config]() {
        AcquisitionService service;
        return service.capture(config);
    }));
}

void MainWindow::runAutoCalibration()
{
    if (autoCalibrationWatcher_.isRunning()) {
        return;
    }

    IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    config.calibrationInput = parameterPanel_->calibrationInput();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = parameterPanel_->projectConfig().outputDirectory;
    }

    setBusy(true, QString::fromUtf8("自动标定中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("自动标定中..."));
    autoCalibrationWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return integratedCalibrationCaptureService_.run(config);
    }));
}

void MainWindow::runScanAndReconstruct()
{
    if (scanWorkflowWatcher_.isRunning()) {
        return;
    }

    IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    config.calibrationInput = parameterPanel_->calibrationInput();
    const auto projectConfig = parameterPanel_->projectConfig();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = projectConfig.outputDirectory;
    }

    const ReconstructionInput reconstructionInput = parameterPanel_->reconstructionInput(CalibrationResult{});
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
        return integratedScanService_.runScanAndReconstruct(config);
    }));
}

void MainWindow::startCalibrationCapture()
{
    if (calibrationCaptureStartWatcher_.isRunning() || calibrationCaptureSessionService_.isActive()) {
        return;
    }

    StereoCameraConfig config = acquisitionPanel_->stereoCameraConfig();
    if (config.outputDirectory.empty()) {
        config.outputDirectory = parameterPanel_->projectConfig().outputDirectory;
    }

    setBusy(true, QString::fromUtf8("开始标定采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在连接相机并创建标定采集会话..."));
    calibrationCaptureStartWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return calibrationCaptureSessionService_.start(config);
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
        return calibrationCaptureSessionService_.captureCurrentFrame();
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

    CalibrationInput input = parameterPanel_->calibrationInput();
    input.leftDirectory = calibrationCapture_.leftDirectory;
    input.rightDirectory = calibrationCapture_.rightDirectory;
    setBusy(true, QString::fromUtf8("标定当前采集帧..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在标定当前采集帧..."));
    calibrationWatcher_.setFuture(QtConcurrent::run([input]() {
        CalibrationService service;
        return service.calibrate(input);
    }));
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
    const auto projectConfig = parameterPanel_->projectConfig();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = projectConfig.outputDirectory;
    }

    setBusy(true, QString::fromUtf8("重建采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在采集重建图像序列..."));
    acquisitionPanel_->setReconstructionCaptureReady(false);
    reconstructionCaptureWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return reconstructionCaptureSessionService_.capture(config);
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
        const auto file = parameterPanel_->projectConfig().calibrationFile;
        calibrationService_.loadCalibration(file, calibration_);
    }
    if (!calibration_.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("缺少标定"), QString::fromUtf8("请先完成标定或加载有效标定文件。"));
        return;
    }

    ReconstructionInput input = parameterPanel_->reconstructionInput(calibration_);
    input.leftDirectory = reconstructionCapture_.leftDirectory;
    input.rightDirectory = reconstructionCapture_.rightDirectory;
    autoExportReconstructionOnFinish_ = true;
    setBusy(true, QString::fromUtf8("重建当前采集帧..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在重建当前采集帧..."));
    reconstructionWatcher_.setFuture(QtConcurrent::run([input]() {
        ReconstructionService service;
        return service.reconstruct(input);
    }));
}

/*
    函数功能：处理后台标定任务完成后的 UI 更新
    输入：
        无
    输出：
        无（函数会恢复忙碌状态、保存标定结果并弹出提示）
*/
void MainWindow::onCalibrationFinished()
{
    setBusy(false, QString());
    try {
        calibration_ = calibrationWatcher_.result();
        if (!calibrationCapture_.leftDirectory.empty() && !calibrationCapture_.rightDirectory.empty()) {
            parameterPanel_->setCalibrationDirectories(calibrationCapture_.leftDirectory, calibrationCapture_.rightDirectory);
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
        if (acquisition_.success) {
            parameterPanel_->setReconstructionDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
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
        parameterPanel_->setCalibrationDirectories(calibrationCapture_.leftDirectory, calibrationCapture_.rightDirectory);
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
        if (reconstructionCapture_.success) {
            parameterPanel_->setReconstructionDirectories(reconstructionCapture_.leftDirectory, reconstructionCapture_.rightDirectory);
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
        if (autoCalibrationWorkflow_.success) {
            calibration_ = autoCalibrationWorkflow_.calibration;
            parameterPanel_->setCalibrationDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
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
        if (scanWorkflow_.success) {
            reconstruction_ = scanWorkflow_.reconstruction;
            calibration_ = scanWorkflow_.calibration;
            pointCloudView_->setPoints(reconstruction_.mergedPoints);
            if (!reconstruction_.frames.empty()) {
                debugImageView_->setImage(reconstruction_.frames.front().leftLinePreview);
            }
            parameterPanel_->setReconstructionDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
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

    parameterPanel_ = new ParameterPanel;
    auto* parameterDock = new QDockWidget(QString::fromUtf8("参数监控"), this);
    parameterDock->setWidget(parameterPanel_);
    addDockWidget(Qt::RightDockWidgetArea, parameterDock);

    acquisitionPanel_ = new AcquisitionPanel;
    auto* acquisitionDock = new QDockWidget(QString::fromUtf8("在线采集"), this);
    acquisitionDock->setWidget(acquisitionPanel_);
    addDockWidget(Qt::RightDockWidgetArea, acquisitionDock);
    tabifyDockWidget(parameterDock, acquisitionDock);
    parameterDock->raise();

    logPanel_ = new LogPanel;
    auto* bottomDock = new QDockWidget(QString::fromUtf8("消息"), this);
    bottomDock->setWidget(logPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
}

// 中央区域以标签页形式承载点云视图、左右图像和调试图像。
void MainWindow::buildCentralView()
{
    auto* tabs = new QTabWidget;
    pointCloudView_ = new PointCloudViewWidget;
    leftImageView_ = new ImageViewWidget;
    rightImageView_ = new ImageViewWidget;
    debugImageView_ = new ImageViewWidget;
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
    const auto config = parameterPanel_ ? parameterPanel_->projectConfig() : AppProjectConfig{};
    auto* calibrationNode = new QTreeWidgetItem(projectTree_, { QString::fromUtf8("标定数据") });
    calibrationNode->addChild(new QTreeWidgetItem({ QString::fromStdString(config.leftCalibrationDirectory) }));
    calibrationNode->addChild(new QTreeWidgetItem({ QString::fromStdString(config.rightCalibrationDirectory) }));

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

// 导出路径默认基于当前项目输出目录拼接，减少每次导出时重复选路径。
QString MainWindow::outputPath(const QString& filename) const
{
    const auto config = parameterPanel_->projectConfig();
    return QDir(QString::fromStdString(config.outputDirectory)).filePath(filename);
}

} // namespace htmsr::app
