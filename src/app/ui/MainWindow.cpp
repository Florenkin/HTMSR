#include "app/ui/MainWindow.h"

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

    Logger::instance().info("App", "HTMSR started.");
}

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
}

void MainWindow::runCalibration()
{
    if (calibrationWatcher_.isRunning()) {
        return;
    }

    const CalibrationInput input = parameterPanel_->calibrationInput();
    setBusy(true, QString::fromUtf8("标定中"));
    calibrationWatcher_.setFuture(QtConcurrent::run([input]() {
        CalibrationService service;
        return service.calibrate(input);
    }));
}

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
    setBusy(true, QString::fromUtf8("重建中"));
    reconstructionWatcher_.setFuture(QtConcurrent::run([input]() {
        ReconstructionService service;
        return service.reconstruct(input);
    }));
}

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
    pointCloudService_.saveTxt(file.toStdString(), reconstruction_.mergedPoints);
}

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
    pointCloudService_.savePcd(file.toStdString(), reconstruction_.mergedPoints);
}

void MainWindow::saveProjectSettings()
{
    configService_.save(parameterPanel_->projectConfig());
    refreshProjectTree();
    Logger::instance().info("App", "Project settings saved.");
}

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

void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件"));
    fileMenu->addAction(QString::fromUtf8("保存项目"), this, &MainWindow::saveProjectSettings);
    fileMenu->addAction(QString::fromUtf8("加载标定"), this, &MainWindow::loadCalibration);
    fileMenu->addSeparator();
    fileMenu->addAction(QString::fromUtf8("退出"), qApp, &QApplication::quit);

    auto* scanMenu = menuBar()->addMenu(QString::fromUtf8("扫描"));
    scanMenu->addAction(QString::fromUtf8("双目标定"), this, &MainWindow::runCalibration);
    scanMenu->addAction(QString::fromUtf8("三维重建"), this, &MainWindow::runReconstruction);

    auto* viewMenu = menuBar()->addMenu(QString::fromUtf8("显示"));
    viewMenu->addAction(QString::fromUtf8("清空点云"), pointCloudView_, &PointCloudViewWidget::clear);

    menuBar()->addMenu(QString::fromUtf8("设置"));
    menuBar()->addMenu(QString::fromUtf8("帮助"));
}

void MainWindow::buildToolBar()
{
    auto* toolbar = addToolBar(QString::fromUtf8("工具"));
    toolbar->setMovable(false);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QString::fromUtf8("保存"), this, &MainWindow::saveProjectSettings);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), QString::fromUtf8("加载标定"), this, &MainWindow::loadCalibration);
    toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QString::fromUtf8("标定"), this, &MainWindow::runCalibration);
    toolbar->addAction(style()->standardIcon(QStyle::SP_ComputerIcon), QString::fromUtf8("重建"), this, &MainWindow::runReconstruction);
    toolbar->addSeparator();
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QString::fromUtf8("TXT"), this, &MainWindow::exportTxt);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DriveHDIcon), QString::fromUtf8("PCD"), this, &MainWindow::exportPcd);
}

void MainWindow::buildDocks()
{
    projectTree_ = new QTreeWidget;
    projectTree_->setHeaderLabel(QString::fromUtf8("项目"));

    auto* leftDock = new QDockWidget(QString::fromUtf8("资源"), this);
    leftDock->setWidget(projectTree_);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    parameterPanel_ = new ParameterPanel;
    auto* rightDock = new QDockWidget(QString::fromUtf8("参数监控"), this);
    rightDock->setWidget(parameterPanel_);
    addDockWidget(Qt::RightDockWidgetArea, rightDock);

    logPanel_ = new LogPanel;
    auto* bottomDock = new QDockWidget(QString::fromUtf8("消息"), this);
    bottomDock->setWidget(logPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
}

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
    auto* resultNode = new QTreeWidgetItem(projectTree_, { QString::fromUtf8("结果点云") });
    resultNode->addChild(new QTreeWidgetItem({ QString::fromUtf8("点数: %1").arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size())) }));
    projectTree_->expandAll();
}

void MainWindow::setBusy(bool busy, const QString& text)
{
    progressBar_->setRange(busy ? 0 : 0, busy ? 0 : 100);
    progressBar_->setValue(busy ? 0 : 100);
    statusBar()->showMessage(text);
}

QString MainWindow::outputPath(const QString& filename) const
{
    const auto config = parameterPanel_->projectConfig();
    return QDir(QString::fromStdString(config.outputDirectory)).filePath(filename);
}

} // namespace htmsr::app
