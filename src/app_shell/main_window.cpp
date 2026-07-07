// 文件说明：
// 实现双目离线重建主窗口，组织项目操作、运行流程和结果预览。

#include "app_shell/main_window.h"

#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "app_shell/ribbon_widget.h"
#include "project_core/manifest_service.h"
#include "project_core/project_service.h"
#include "reconstruction_core/reconstruction_session_service.h"
#include "visualization/image_view_widget.h"
#include "visualization/pointcloud_summary_widget.h"

namespace htmsr::app_shell {

MainWindow::MainWindow(
    project_core::ProjectService& projectService,
    project_core::ManifestService& manifestService,
    reconstruction_core::ReconstructionSessionService& sessionService,
    QWidget* parent)
    : QMainWindow(parent),
      projectService_(projectService),
      manifestService_(manifestService),
      sessionService_(sessionService) {
    setupUi();
}

void MainWindow::setupUi() {
    setWindowTitle("HTMSR Stereo");
    resize(1700, 1000);

    // 整体界面分为顶部操作区、左侧项目区和中央结果区。

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);

    ribbonWidget_ = new RibbonWidget(this);
    centralLayout->addWidget(ribbonWidget_);
    connect(ribbonWidget_, &RibbonWidget::newProjectRequested, this, &MainWindow::onNewProjectRequested);
    connect(ribbonWidget_, &RibbonWidget::openProjectRequested, this, &MainWindow::onOpenProjectRequested);
    connect(ribbonWidget_, &RibbonWidget::importManifestRequested, this, &MainWindow::onImportManifestRequested);
    connect(ribbonWidget_, &RibbonWidget::importCalibrationRequested, this, &MainWindow::onImportCalibrationRequested);
    connect(ribbonWidget_, &RibbonWidget::importImagesRequested, this, &MainWindow::onImportImagesRequested);
    connect(ribbonWidget_, &RibbonWidget::runReconstructionRequested, this, &MainWindow::onRunReconstructionRequested);
    connect(ribbonWidget_, &RibbonWidget::exportOutputsRequested, this, &MainWindow::onExportOutputsRequested);
    connect(ribbonWidget_, &RibbonWidget::openSettingsRequested, this, &MainWindow::onOpenSettingsRequested);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    centralLayout->addWidget(splitter, 1);

    // 左侧区域展示项目树、任务列表和运行参数。

    auto* leftPanel = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);

    auto* projectGroup = new QGroupBox("项目与数据树", leftPanel);
    auto* projectLayout = new QVBoxLayout(projectGroup);
    projectTree_ = new QTreeWidget(projectGroup);
    projectTree_->setHeaderLabels({"项目", "值"});
    projectLayout->addWidget(projectTree_);
    leftLayout->addWidget(projectGroup, 2);

    auto* taskGroup = new QGroupBox("任务列表", leftPanel);
    auto* taskLayout = new QVBoxLayout(taskGroup);
    taskList_ = new QListWidget(taskGroup);
    taskLayout->addWidget(taskList_);
    leftLayout->addWidget(taskGroup, 1);

    auto* configGroup = new QGroupBox("重建参数", leftPanel);
    auto* configLayout = new QFormLayout(configGroup);
    centerlineMethodCombo_ = new QComboBox(configGroup);
    centerlineMethodCombo_->addItems({"Gray", "Steger", "MultiThres"});
    bwThresholdSpin_ = new QDoubleSpinBox(configGroup);
    bwThresholdSpin_->setRange(0.0, 255.0);
    bwThresholdSpin_->setValue(55.0);
    sumThresholdSpin_ = new QDoubleSpinBox(configGroup);
    sumThresholdSpin_->setRange(0.0, 1000.0);
    sumThresholdSpin_->setValue(124.0);
    matchingDistanceSpin_ = new QDoubleSpinBox(configGroup);
    matchingDistanceSpin_->setRange(0.0001, 10.0);
    matchingDistanceSpin_->setDecimals(4);
    matchingDistanceSpin_->setValue(0.1);
    configLayout->addRow("中心线算法", centerlineMethodCombo_);
    configLayout->addRow("灰度阈值", bwThresholdSpin_);
    configLayout->addRow("支持阈值", sumThresholdSpin_);
    configLayout->addRow("匹配误差阈值", matchingDistanceSpin_);
    leftLayout->addWidget(configGroup, 1);

    splitter->addWidget(leftPanel);

    // 中央页签用于展示左右原图、中心线、匹配图和点云摘要。

    auto* centerTabs = new QTabWidget(splitter);
    leftImageView_ = new visualization::ImageViewWidget("左原图", centerTabs);
    rightImageView_ = new visualization::ImageViewWidget("右原图", centerTabs);
    leftCenterlineView_ = new visualization::ImageViewWidget("左中心线预览", centerTabs);
    rightCenterlineView_ = new visualization::ImageViewWidget("右中心线预览", centerTabs);
    matchView_ = new visualization::ImageViewWidget("双目匹配预览", centerTabs);
    pointcloudView_ = new visualization::PointcloudSummaryWidget(centerTabs);

    resultTable_ = new QTableWidget(centerTabs);
    resultTable_->setColumnCount(7);
    resultTable_->setHorizontalHeaderLabels({"帧", "状态", "左点数", "右点数", "匹配数", "三维点数", "消息"});
    resultTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    centerTabs->addTab(leftImageView_, "左原图");
    centerTabs->addTab(rightImageView_, "右原图");
    centerTabs->addTab(leftCenterlineView_, "左中心线");
    centerTabs->addTab(rightCenterlineView_, "右中心线");
    centerTabs->addTab(matchView_, "匹配预览");
    centerTabs->addTab(pointcloudView_, "点云摘要");
    centerTabs->addTab(resultTable_, "结果表");
    splitter->addWidget(centerTabs);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    auto* logDock = new QDockWidget("系统日志", this);
    logView_ = new QTextEdit(logDock);
    logView_->setReadOnly(true);
    logDock->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    setCentralWidget(central);

    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_ = new QLabel("未加载项目", this);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(progressBar_);
}

void MainWindow::appendLog(const QString& message) {
    logView_->append(message);
    statusLabel_->setText(message);
}

bool MainWindow::ensureProjectLoaded() const {
    if (projectDocument_.has_value()) {
        return true;
    }
    QMessageBox::warning(const_cast<MainWindow*>(this), "提示", "请先新建或打开项目");
    return false;
}

void MainWindow::writeManifestTemplate(const QString& filePath) const {
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        file.write(manifestService_.manifestTemplate().toUtf8());
    }
}

void MainWindow::updateProjectTree() {
    projectTree_->clear();
    if (!projectDocument_.has_value()) {
        return;
    }

    // 将项目关键路径和状态直接映射到树形视图中。

    const auto& document = projectDocument_.value();
    auto* root = new QTreeWidgetItem(projectTree_, {"项目", document.name});
    new QTreeWidgetItem(root, {"根目录", document.rootPath});
    new QTreeWidgetItem(root, {"Manifest", document.manifestPath});
    new QTreeWidgetItem(root, {"标定", document.calibrationPath});
    new QTreeWidgetItem(root, {"图像目录", document.imageDirectory});
    new QTreeWidgetItem(root, {"最近状态", document.lastRunStatus});
    root->setExpanded(true);
}

void MainWindow::updateSessionViews(const data_model::ReconstructionSession& session) {
    // 结果表按帧展示左右提线、匹配和三维重建情况。

    resultTable_->setRowCount(session.frameResults.size());
    taskList_->clear();

    for (int row = 0; row < session.frameResults.size(); ++row) {
        const auto& frame = session.frameResults[row];
        const QString frameName = QFileInfo(frame.manifestRow.leftImagePath).completeBaseName();
        taskList_->addItem(QString("%1 | %2").arg(frameName, frame.succeeded ? "成功" : "失败"));
        resultTable_->setItem(row, 0, new QTableWidgetItem(frameName));
        resultTable_->setItem(row, 1, new QTableWidgetItem(frame.succeeded ? "成功" : "失败"));
        resultTable_->setItem(row, 2, new QTableWidgetItem(QString::number(frame.leftCenterline.points.size())));
        resultTable_->setItem(row, 3, new QTableWidgetItem(QString::number(frame.rightCenterline.points.size())));
        resultTable_->setItem(row, 4, new QTableWidgetItem(QString::number(frame.stereoMatch.pairCount)));
        resultTable_->setItem(row, 5, new QTableWidgetItem(QString::number(frame.pointCloud.points.size())));
        resultTable_->setItem(row, 6, new QTableWidgetItem(frame.message));
    }

    // 默认展示第一帧结果，便于快速确认整个流程是否跑通。

    if (!session.frameResults.isEmpty()) {
        const auto& first = session.frameResults.front();
        leftImageView_->setImage(matToImage(cv::imread(first.manifestRow.leftImagePath.toStdString(), cv::IMREAD_COLOR)));
        rightImageView_->setImage(matToImage(cv::imread(first.manifestRow.rightImagePath.toStdString(), cv::IMREAD_COLOR)));
        leftCenterlineView_->setImage(matToImage(cv::imread(first.leftCenterline.previewPath.toStdString(), cv::IMREAD_COLOR)));
        rightCenterlineView_->setImage(matToImage(cv::imread(first.rightCenterline.previewPath.toStdString(), cv::IMREAD_COLOR)));
        matchView_->setImage(matToImage(cv::imread(first.stereoMatch.previewPath.toStdString(), cv::IMREAD_COLOR)));
        pointcloudView_->setPoints(first.pointCloud.points);
    } else {
        leftImageView_->setMessage("暂无左图像");
        rightImageView_->setMessage("暂无右图像");
        leftCenterlineView_->setMessage("暂无左中心线");
        rightCenterlineView_->setMessage("暂无右中心线");
        matchView_->setMessage("暂无匹配预览");
        pointcloudView_->clearView();
    }
}

QImage MainWindow::matToImage(const cv::Mat& image) {
    if (image.empty()) {
        return {};
    }
    cv::Mat rgb;
    if (image.channels() == 3) {
        cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
    } else {
        cv::cvtColor(image, rgb, cv::COLOR_GRAY2RGB);
    }
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

void MainWindow::onNewProjectRequested() {
    const QString parentDir = QFileDialog::getExistingDirectory(this, "选择项目父目录");
    if (parentDir.isEmpty()) {
        return;
    }

    bool ok = false;
    const QString projectName = QInputDialog::getText(this, "新建项目", "项目名称", QLineEdit::Normal, "HTMSR_Stereo_Project", &ok);
    if (!ok || projectName.trimmed().isEmpty()) {
        return;
    }

    project_core::ProjectDocument document;
    QString error;
    if (!projectService_.createProject(parentDir, projectName.trimmed(), document, error)) {
        QMessageBox::critical(this, "创建失败", error);
        return;
    }

    writeManifestTemplate(document.manifestPath);
    projectDocument_ = document;
    updateProjectTree();
    appendLog(QString("项目已创建: %1").arg(document.rootPath));
}

void MainWindow::onOpenProjectRequested() {
    const QString projectFile = QFileDialog::getOpenFileName(this, "打开项目", QString(), "Project JSON (*.json)");
    if (projectFile.isEmpty()) {
        return;
    }

    QString error;
    const auto document = projectService_.openProject(projectFile, error);
    if (!document.has_value()) {
        QMessageBox::critical(this, "打开失败", error);
        return;
    }

    projectDocument_ = document;
    updateProjectTree();
    appendLog(QString("项目已打开: %1").arg(document->rootPath));
}

void MainWindow::onImportManifestRequested() {
    if (!ensureProjectLoaded()) {
        return;
    }
    const QString file = QFileDialog::getOpenFileName(this, "导入双目 Manifest", QString(), "CSV (*.csv)");
    if (file.isEmpty()) {
        return;
    }

    QString error;
    if (!projectService_.importManifest(*projectDocument_, file, error)) {
        QMessageBox::critical(this, "导入失败", error);
        return;
    }

    updateProjectTree();
    appendLog(QString("Manifest 已导入: %1").arg(projectDocument_->manifestPath));
}

void MainWindow::onImportCalibrationRequested() {
    if (!ensureProjectLoaded()) {
        return;
    }
    const QString file = QFileDialog::getOpenFileName(this, "导入双目标定", QString(), "YAML (*.yml *.yaml)");
    if (file.isEmpty()) {
        return;
    }

    QString error;
    if (!projectService_.importCalibration(*projectDocument_, file, error)) {
        QMessageBox::critical(this, "导入失败", error);
        return;
    }

    updateProjectTree();
    appendLog(QString("标定文件已导入: %1").arg(projectDocument_->calibrationPath));
}

void MainWindow::onImportImagesRequested() {
    if (!ensureProjectLoaded()) {
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this, "导入双目图像目录");
    if (dir.isEmpty()) {
        return;
    }

    QString error;
    if (!projectService_.importImageDirectory(*projectDocument_, dir, error)) {
        QMessageBox::critical(this, "导入失败", error);
        return;
    }

    updateProjectTree();
    appendLog(QString("图像目录已导入: %1").arg(projectDocument_->imageDirectory));
}

void MainWindow::onRunReconstructionRequested() {
    if (!ensureProjectLoaded()) {
        return;
    }

    // 将当前界面参数回写到项目配置中。

    auto& config = projectDocument_->lastConfig;
    config.centerlineMethod = static_cast<data_model::CenterlineMethod>(centerlineMethodCombo_->currentIndex());
    config.bwThr = bwThresholdSpin_->value();
    config.sumThr = sumThresholdSpin_->value();
    config.matchingDistance = matchingDistanceSpin_->value();
    config.calibrationPath = projectDocument_->calibrationPath;

    QString error;
    projectService_.saveProject(*projectDocument_, error);

    // 调用会话服务执行完整离线重建流程。

    QStringList logs;
    data_model::ReconstructionSession session;
    progressBar_->setValue(10);
    const bool ok = sessionService_.run(*projectDocument_, session, logs);
    progressBar_->setValue(100);

    for (const QString& log : logs) {
        appendLog(log);
    }

    if (!ok) {
        QMessageBox::warning(this, "重建失败", "未能完成双目离线重建，请查看日志");
        projectDocument_->lastRunStatus = "Failed";
        projectService_.saveProject(*projectDocument_, error);
        updateProjectTree();
        return;
    }

    lastSession_ = session;
    projectDocument_->lastRunStatus = QString("Completed (%1 pairs)").arg(session.frameResults.size());
    projectDocument_->recentOutputFiles = {session.mergedPointCloudPath, session.mergedPoiPath};
    projectService_.saveProject(*projectDocument_, error);
    updateProjectTree();
    updateSessionViews(session);
    appendLog("双目离线重建完成");
}

void MainWindow::onExportOutputsRequested() {
    if (!lastSession_.has_value()) {
        QMessageBox::information(this, "提示", "当前还没有可导出的运行结果");
        return;
    }
    QMessageBox::information(
        this,
        "导出结果",
        QString("输出目录已生成:\n%1\n\n合并点云:\n%2\n合并 POI:\n%3")
            .arg(projectService_.outputDirectory(*projectDocument_), lastSession_->mergedPointCloudPath, lastSession_->mergedPoiPath));
}

void MainWindow::onOpenSettingsRequested() {
    QMessageBox::information(this, "系统设置", "v1 先以内嵌参数面板为主，后续可扩展为独立设置页。");
}

}  // namespace htmsr::app_shell
