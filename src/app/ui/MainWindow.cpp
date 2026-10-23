#include "app/ui/MainWindowTasks.h"
#include "app/ui/MainWindowViewModel.h"

namespace htmsr::app {
using namespace detail;

MainWindow::MainWindow(QWidget* parent, StartupMode startupMode)
    : SARibbonMainWindow(parent)
{
    setWindowTitle(QString::fromUtf8("HTMSR · 双目三维扫描"));
    setWindowIcon(QIcon(QStringLiteral(":/htmsr/icons/app.svg")));
    resize(1440, 920);
    setDockNestingEnabled(true);

    logSink_ = new QtLogSink(this);
    connect(logSink_, &QtLogSink::messageReceived, this, [this](const LogMessage& message) {
        logPanel_->appendMessage(message);
    }, Qt::QueuedConnection);

    // 按参考界面布局依次构建中央视图和 Dock 区域。
    buildCentralView();
    buildDocks();
    buildRibbon();
    applyWindowTheme();

    progressBar_ = new QProgressBar;
    progressBar_->setObjectName("taskProgress");
    progressBar_->setMaximumWidth(180);
    progressBar_->setMaximumHeight(16);
    progressBar_->setTextVisible(false);
    progressBar_->setValue(0);
    galvoStatusLabel_ = new QLabel;
    galvoStatusLabel_->setMinimumWidth(0);
    galvoStatusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addPermanentWidget(galvoStatusLabel_, 1);
    statusBar()->addPermanentWidget(progressBar_);

    // 自动回归必须在配置恢复、自动保存及任何设备信号连接之前返回。
    if (startupMode == StartupMode::InterfaceTest)
        return;

    acquisitionPanel_->setProjectConfig(configService_.load());
    configAutoSave_ = new ConfigAutoSave([this]() {
        if (!configService_.save(acquisitionPanel_->projectConfig()))
            statusBar()->showMessage(QString::fromUtf8("参数保存失败：%1").arg(configService_.lastError()));
    }, this);
    connect(acquisitionPanel_, &AcquisitionPanel::projectConfigChanged, configAutoSave_, &ConfigAutoSave::schedule);

    connect(&calibrationWatcher_, &QFutureWatcher<CalibrationResult>::finished, this, &MainWindow::onCalibrationFinished);
    connect(&reconstructionWatcher_, &QFutureWatcher<ReconstructionTaskResult>::finished, this, &MainWindow::onReconstructionFinished);
    connect(&calibrationFrameWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onCalibrationFrameCaptured);
    connect(&reconstructionCaptureWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onReconstructionCaptureFinished);
    connect(&reconstructionGalvoPreflightWatcher_, &QFutureWatcher<GalvoMotionVerificationResult>::finished, this, &MainWindow::onReconstructionGalvoPreflightFinished);
    connect(&galvoMotionParametersWatcher_, &QFutureWatcher<GalvoMotionVerificationResult>::finished, this, &MainWindow::onGalvoMotionParametersApplied);
    connect(&serialCommandPackWatcher_, &QFutureWatcher<SerialCommandPackSendResult>::finished, this, &MainWindow::onSerialCommandPackFinished);
    // 恢复已有标定结果只读取文件，不触发采集或振镜动作。
    const auto restoredCalibration = acquisitionPanel_->projectConfig().calibrationFile;
    if (!restoredCalibration.empty()) {
        try {
            calibrationService_.loadCalibration(restoredCalibration, calibration_);
        } catch (const std::exception& ex) {
            Logger::instance().warning("Config", "无法恢复标定结果：" + std::string(ex.what()));
        }
        updateResultAvailability();
    }
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
    updateResultAvailability();
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
    shutdownCancellation_.request();
    disconnect(logSink_, nullptr, this, nullptr);
    disconnect(&calibrationWatcher_, nullptr, this, nullptr);
    disconnect(&reconstructionWatcher_, nullptr, this, nullptr);
    disconnect(&calibrationFrameWatcher_, nullptr, this, nullptr);
    disconnect(&reconstructionCaptureWatcher_, nullptr, this, nullptr);
    disconnect(&reconstructionGalvoPreflightWatcher_, nullptr, this, nullptr);
    disconnect(&livePreviewWatcher_, nullptr, this, nullptr);
    disconnect(&galvoMotionParametersWatcher_, nullptr, this, nullptr);
    disconnect(&serialCommandPackWatcher_, nullptr, this, nullptr);
    serialCommandPackStopRequested_->store(true);
    stopLivePreview();
    if (calibrationWatcher_.isRunning()) {

        calibrationWatcher_.waitForFinished();
    }
    if (reconstructionWatcher_.isRunning()) {

        reconstructionWatcher_.waitForFinished();
    }
    if (calibrationFrameWatcher_.isRunning()) {

        calibrationFrameWatcher_.waitForFinished();
    }
    if (reconstructionCaptureWatcher_.isRunning()) {

        reconstructionCaptureWatcher_.waitForFinished();
    }
    if (reconstructionGalvoPreflightWatcher_.isRunning()) {

        reconstructionGalvoPreflightWatcher_.waitForFinished();
    }
    if (galvoMotionParametersWatcher_.isRunning()) {

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
    if (!shuttingDown_.exchange(true)) {
        acquisitionPanel_->commitPendingEdits();
        if (configAutoSave_)
            configAutoSave_->flush();
        shutdownCancellation_.request();
        serialCommandPackStopRequested_->store(true);
        livePreviewStopRequested_.store(true);
        centralWidget()->setEnabled(false);
        acquisitionPanel_->setEnabled(false);
        ribbonBar()->setEnabled(false);
        statusBar()->showMessage(QString::fromUtf8("正在停止任务并释放设备，请稍候…"));
    }
    if (calibrationWatcher_.isRunning() || reconstructionWatcher_.isRunning() ||
        calibrationFrameWatcher_.isRunning() || reconstructionCaptureWatcher_.isRunning() ||
        reconstructionGalvoPreflightWatcher_.isRunning() || livePreviewWatcher_.isRunning() ||
        galvoMotionParametersWatcher_.isRunning() || serialCommandPackWatcher_.isRunning()) {
        event->ignore();
        QTimer::singleShot(50, this, [this]() { close(); });
        return;
    }
    SARibbonMainWindow::closeEvent(event);
}

// Dock 区域保留右侧在线工作流和底部日志面板，主显示区留给图像与点云。
void MainWindow::buildDocks()
{
    acquisitionPanel_ = new AcquisitionPanel;
    serialCommandPackWidget_->setRawGalvoCommandWidget(acquisitionPanel_->rawGalvoCommandWidget());
    auto* acquisitionDock = new QDockWidget(QString::fromUtf8("在线工作流"), this);
    acquisitionDock_ = acquisitionDock;
    acquisitionDock->setObjectName("acquisitionDock");
    acquisitionDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    acquisitionDock->setWidget(acquisitionPanel_);
    addDockWidget(Qt::RightDockWidgetArea, acquisitionDock);

    // 默认保留两行可见空间，面板会自动滚动到最新记录。
    logPanel_ = new LogPanel;
    auto* bottomDock = new QDockWidget(QString::fromUtf8("消息"), this);
    logDock_ = bottomDock;
    bottomDock->setObjectName("logDock");
    bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    bottomDock->setWidget(logPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
    QTimer::singleShot(0, this, [this, bottomDock]() {
        const int titleHeight = bottomDock->height() - logPanel_->height();
        resizeDocks({bottomDock}, {logPanel_->sizeHint().height() + titleHeight + 8}, Qt::Vertical);
    });
}

// 中央区域以标签页形式承载双目实时预览、采集筛选和点云视图。
void MainWindow::buildCentralView()
{
    auto* tabs = new QTabWidget;
    centralTabs_ = tabs;
    tabs->setObjectName("centralTabs");
    tabs->setDocumentMode(true);
    tabs->setIconSize(QSize(18, 18));
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
    stereoDivider->setStyleSheet("background: #dce4ed;");

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
    laserExtractionReviewWidget_ = new CaptureReviewWidget(CaptureReviewWidget::Mode::LaserExtractionReadOnly);
    laserExtractionReviewWidget_->setObjectName("laserExtractionReview");
    serialCommandPackWidget_ = new SerialCommandPackWidget;

    pointCloudView_ = new PointCloudViewWidget;
    const QStringList tabTitles = mainWindowCentralTabTitles();
    tabs->addTab(liveStereoPage, tabTitles.at(0));
    tabs->addTab(pointCloudView_, tabTitles.at(1));
    tabs->addTab(captureReviewWidget_, tabTitles.at(2));
    tabs->addTab(laserExtractionReviewWidget_, tabTitles.at(3));
    tabs->addTab(serialCommandPackWidget_, tabTitles.at(4));
    const QStringList icons = {"camera", "cloud", "capture", "laser", "command"};
    for (int index = 0; index < tabs->count(); ++index)
        tabs->setTabIcon(index, QIcon(QStringLiteral(":/htmsr/icons/%1.svg").arg(icons.at(index))));
    setCentralWidget(tabs);
}

void MainWindow::updateResultAvailability()
{
    if (loadCalibrationAction_)
        loadCalibrationAction_->setEnabled(!busy_ && !shuttingDown_.load());
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
    if (laserExtractionReviewWidget_) {
        laserExtractionReviewWidget_->setEnabled(!busy);
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

} // namespace htmsr::app
