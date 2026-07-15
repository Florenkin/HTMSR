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
    connect(acquisitionPanel_, &AcquisitionPanel::refreshDevicesRequested, this, &MainWindow::refreshAcquisitionDevices);
    connect(acquisitionPanel_, &AcquisitionPanel::captureRequested, this, &MainWindow::runAcquisition);

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
        pointCloudView_->setPoints(reconstruction_.mergedPoints);
        if (!reconstruction_.frames.empty()) {
            leftImageView_->setImage(reconstruction_.frames.front().leftLinePreview);
            rightImageView_->setImage(reconstruction_.frames.front().rightLinePreview);
            debugImageView_->setImage(reconstruction_.frames.front().leftLinePreview);
        }
        refreshProjectTree();
        QMessageBox::information(this, QString::fromUtf8("重建完成"), QString::fromUtf8("点云数量: %1").arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size())));
    } catch (const std::exception& ex) {
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
    scanMenu->addAction(QString::fromUtf8("在线采集"), this, &MainWindow::runAcquisition);
    scanMenu->addSeparator();
    scanMenu->addAction(QString::fromUtf8("双目标定"), this, &MainWindow::runCalibration);
    scanMenu->addAction(QString::fromUtf8("三维重建"), this, &MainWindow::runReconstruction);

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
    toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QString::fromUtf8("采集"), this, &MainWindow::runAcquisition);
    toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QString::fromUtf8("标定"), this, &MainWindow::runCalibration);
    toolbar->addAction(style()->standardIcon(QStyle::SP_ComputerIcon), QString::fromUtf8("重建"), this, &MainWindow::runReconstruction);
    toolbar->addSeparator();
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QString::fromUtf8("TXT"), this, &MainWindow::exportTxt);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DriveHDIcon), QString::fromUtf8("PCD"), this, &MainWindow::exportPcd);
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
