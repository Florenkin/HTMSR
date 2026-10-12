#include "app/ui/MainWindow.h"

#include "app/ui/AcquisitionPanel.h"
#include "app/ui/ImageViewWidget.h"
#include "app/ui/LogPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QFile>
#include <QKeySequence>
#include <QMessageBox>
#include <QTabWidget>
#include <QTimer>

namespace htmsr::app {
namespace {
QIcon ribbonIcon(const char* name)
{
    return QIcon(QStringLiteral(":/htmsr/icons/%1.svg").arg(QString::fromLatin1(name)));
}
}

void MainWindow::applyWindowTheme()
{
    setRibbonTheme(SARibbonTheme::RibbonThemeOffice2013);
    QFile theme(QStringLiteral(":/htmsr/theme/light.qss"));
    if (theme.open(QIODevice::ReadOnly))
        setStyleSheet(styleSheet() + QString::fromUtf8(theme.readAll()));
    ribbonBar()->setTabBarBaseLineColor(QColor(QStringLiteral("#dce4ed")));
    ribbonBar()->setWindowTitleTextColor(QColor(QStringLiteral("#29435e")));
}

void MainWindow::showCentralPage(int index)
{
    if (index >= 0 && index < centralTabs_->count())
        centralTabs_->setCurrentIndex(index);
}

/*
    函数功能：构建功能区，复用工作流动作并连接文件、视图和面板入口
    输入：
        无；中央视图和 Dock 面板必须已经创建
    输出：
        副作用：添加功能区页、快捷操作及文件菜单，默认显示采集与标定页
*/
void MainWindow::buildRibbon()
{
    auto* ribbon = ribbonBar();
    ribbon->setObjectName(QStringLiteral("mainRibbon"));
    ribbon->setRibbonStyle(SARibbonBar::RibbonStyleCompactThreeRow);
    ribbon->setTitleBarHeight(40);
    ribbon->setTabBarHeight(34);
    ribbon->showMinimumModeButton(true);

    using WorkflowAction = AcquisitionPanel::WorkflowAction;
    const auto workflow = [this](WorkflowAction id, const char* icon) {
        auto* action = acquisitionPanel_->workflowAction(id);
        action->setIcon(ribbonIcon(icon));
        return action;
    };
    auto* refresh = workflow(WorkflowAction::RefreshDevices, "refresh");
    auto* captureCalibration = workflow(WorkflowAction::CaptureCalibration, "capture");
    auto* calibrate = workflow(WorkflowAction::Calibrate, "calibration");
    auto* exportCalibration = workflow(WorkflowAction::ExportCalibration, "export");
    auto* captureReconstruction = workflow(WorkflowAction::CaptureReconstruction, "scan");
    auto* reconstruct = workflow(WorkflowAction::Reconstruct, "cloud");
    auto* exportReconstruction = workflow(WorkflowAction::ExportReconstruction, "export");

    loadCalibrationAction_ = new QAction(ribbonIcon("open"), QString::fromUtf8("加载标定"), this);
    loadCalibrationAction_->setObjectName(QStringLiteral("loadCalibrationAction"));
    loadCalibrationAction_->setToolTip(QString::fromUtf8("加载已有的 YAML 标定文件。"));
    loadCalibrationAction_->setShortcut(QKeySequence::Open);
    addAction(loadCalibrationAction_);
    connect(loadCalibrationAction_, &QAction::triggered, this, &MainWindow::loadCalibration);

    auto* fileCategory = ribbon->addCategoryPage(QString::fromUtf8("文件"));
    fileCategory->setObjectName(QStringLiteral("fileCategory"));
    auto* calibrationFiles = fileCategory->addPanel(QString::fromUtf8("标定文件"));
    calibrationFiles->addLargeAction(loadCalibrationAction_);
    calibrationFiles->addLargeAction(exportCalibration);
    auto* resultFiles = fileCategory->addPanel(QString::fromUtf8("重建结果"));
    resultFiles->addLargeAction(exportReconstruction);

    auto* fileMenu = new SARibbonMenu(this);
    fileMenu->addAction(loadCalibrationAction_);
    fileMenu->addAction(exportCalibration);
    fileMenu->addAction(exportReconstruction);
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction(QString::fromUtf8("退出"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    if (auto* applicationButton = qobject_cast<QToolButton*>(ribbon->applicationButton())) {
        applicationButton->setObjectName(QStringLiteral("htmsrApplicationButton"));
        applicationButton->setText(QStringLiteral("HTMSR"));
        applicationButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
        applicationButton->setPopupMode(QToolButton::InstantPopup);
        applicationButton->setMenu(fileMenu);
    }

    auto* acquisitionCategory = ribbon->addCategoryPage(QString::fromUtf8("采集与标定"));
    acquisitionCategory->setObjectName(QStringLiteral("acquisitionCategory"));
    auto* devices = acquisitionCategory->addPanel(QString::fromUtf8("设备"));
    devices->addLargeAction(refresh);
    auto* calibration = acquisitionCategory->addPanel(QString::fromUtf8("双目标定"));
    calibration->addLargeAction(captureCalibration);
    calibration->addLargeAction(calibrate);
    calibration->addLargeAction(loadCalibrationAction_);
    calibration->addSmallAction(exportCalibration);

    auto* reconstructionCategory = ribbon->addCategoryPage(QString::fromUtf8("重建"));
    reconstructionCategory->setObjectName(QStringLiteral("reconstructionCategory"));
    auto* scan = reconstructionCategory->addPanel(QString::fromUtf8("扫描与重建"));
    scan->addLargeAction(captureReconstruction);
    scan->addLargeAction(reconstruct);
    scan->addLargeAction(exportReconstruction);

    auto* viewCategory = ribbon->addCategoryPage(QString::fromUtf8("视图"));
    viewCategory->setObjectName(QStringLiteral("viewCategory"));
    auto* pages = viewCategory->addPanel(QString::fromUtf8("工作区"));
    auto* pageGroup = new QActionGroup(this);
    pageGroup->setExclusive(true);
    const QStringList titles = {
        QString::fromUtf8("双目预览"), QString::fromUtf8("点云视图"), QString::fromUtf8("采集筛选"),
        QString::fromUtf8("激光线"), QString::fromUtf8("串口命令")
    };
    const char* icons[] = {"camera", "cloud", "capture", "laser", "command"};
    for (int index = 0; index < centralTabs_->count(); ++index) {
        auto* action = new QAction(ribbonIcon(icons[index]), titles.at(index), pageGroup);
        action->setObjectName(QStringLiteral("showCentralPage%1Action").arg(index));
        action->setCheckable(true);
        action->setChecked(centralTabs_->currentIndex() == index);
        pages->addLargeAction(action);
        connect(action, &QAction::triggered, this, [this, index]() { showCentralPage(index); });
        connect(centralTabs_, &QTabWidget::currentChanged, action, [action, index](int current) {
            action->setChecked(current == index);
        });
    }

    auto* panels = viewCategory->addPanel(QString::fromUtf8("面板"));
    auto* workflowPanel = acquisitionDock_->toggleViewAction();
    workflowPanel->setIcon(ribbonIcon("panel"));
    workflowPanel->setText(QString::fromUtf8("参数面板"));
    panels->addSmallAction(workflowPanel);
    auto* logPanel = logDock_->toggleViewAction();
    logPanel->setIcon(ribbonIcon("log"));
    logPanel->setText(QString::fromUtf8("消息面板"));
    panels->addSmallAction(logPanel);
    auto* restoreLayout = new QAction(ribbonIcon("refresh"), QString::fromUtf8("恢复布局"), this);
    restoreLayout->setObjectName(QStringLiteral("restoreLayoutAction"));
    panels->addSmallAction(restoreLayout);
    connect(restoreLayout, &QAction::triggered, this, [this]() {
        acquisitionDock_->setFloating(false);
        logDock_->setFloating(false);
        addDockWidget(Qt::RightDockWidgetArea, acquisitionDock_);
        addDockWidget(Qt::BottomDockWidgetArea, logDock_);
        acquisitionDock_->show();
        logDock_->show();
        ribbonBar()->setMinimumMode(false);
        resizeDocks({acquisitionDock_}, {acquisitionPanel_->sizeHint().width()}, Qt::Horizontal);
        resizeDocks({logDock_}, {logPanel_->sizeHint().height() + 32}, Qt::Vertical);
    });

    auto* display = viewCategory->addPanel(QString::fromUtf8("显示"));
    auto* fitImages = new QAction(ribbonIcon("fit"), QString::fromUtf8("图像适应窗口"), this);
    fitImages->setObjectName(QStringLiteral("fitLiveImagesAction"));
    display->addLargeAction(fitImages);
    connect(fitImages, &QAction::triggered, this, [this]() {
        showCentralPage(0);
        liveLeftImageView_->fitToView();
        liveRightImageView_->fitToView();
    });
    auto* clearMessages = new QAction(ribbonIcon("log"), QString::fromUtf8("清空消息"), this);
    clearMessages->setToolTip(QString::fromUtf8("清空当前消息面板，磁盘日志继续保留。"));
    display->addSmallAction(clearMessages);
    connect(clearMessages, &QAction::triggered, logPanel_, &LogPanel::clear);

    auto* helpCategory = ribbon->addCategoryPage(QString::fromUtf8("帮助"));
    auto* help = helpCategory->addPanel(QString::fromUtf8("使用帮助"));
    auto* helpAction = new QAction(ribbonIcon("help"), QString::fromUtf8("操作指南"), this);
    help->addLargeAction(helpAction);
    connect(helpAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, QString::fromUtf8("HTMSR 操作指南"),
            QString::fromUtf8("1. 刷新设备，在右侧选择左右相机并设置采集参数。\n"
                              "2. 采集标定帧并执行标定，或加载已有标定文件。\n"
                              "3. 在重建页设置振镜及图像目录，扫描采集后执行重建。\n"
                              "4. 在点云、采集和激光线工作区检查结果，再导出文件。\n\n"
                              "图像支持滚轮缩放；双击功能区标签可折叠或展开工具栏。\n"
                              "参数与消息面板可在“视图”中重新显示。"));
    });

    ribbon->quickAccessBar()->addAction(refresh);
    ribbon->quickAccessBar()->addAction(loadCalibrationAction_);
    ribbon->quickAccessBar()->addAction(exportReconstruction);
    connect(ribbon, &SARibbonBar::currentRibbonTabChanged, this, [this](int index) {
        if (index == 1 || index == 2)
            acquisitionPanel_->showWorkflowPage(index - 1);
    });
    ribbon->setCurrentIndex(1);
    // SARibbon 在事件循环完成布局后再次应用主题，保证标题栏和系统按钮一致。
    QTimer::singleShot(0, this, &MainWindow::applyWindowTheme);
}

} // namespace htmsr::app
