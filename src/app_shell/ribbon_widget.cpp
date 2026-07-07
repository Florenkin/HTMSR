// 文件说明：
// 实现顶部 Ribbon 操作区，用于承载项目、导入、运行和导出入口。

#include "app_shell/ribbon_widget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace htmsr::app_shell {

namespace {

QWidget* createGroup(const QString& title, const QList<QToolButton*>& buttons, QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet("QFrame { background: #f8fafc; border: 1px solid #cbd5e1; border-radius: 6px; }");
    auto* layout = new QVBoxLayout(frame);
    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setStyleSheet("QLabel { font-weight: 700; color: #0f172a; }");
    layout->addWidget(titleLabel);
    for (QToolButton* button : buttons) {
        layout->addWidget(button);
    }
    layout->addStretch(1);
    return frame;
}

}  // namespace

RibbonWidget::RibbonWidget(QWidget* parent) : QWidget(parent) {
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    auto* newProjectButton = createButton("新建项目", "创建目录与 project.json");
    auto* openProjectButton = createButton("打开项目", "恢复已有双目重建项目");
    connect(newProjectButton, &QToolButton::clicked, this, &RibbonWidget::newProjectRequested);
    connect(openProjectButton, &QToolButton::clicked, this, &RibbonWidget::openProjectRequested);
    rootLayout->addWidget(createGroup("项目区", {newProjectButton, openProjectButton}, this));

    auto* importManifestButton = createButton("导入 Manifest", "导入双目清单并校验");
    auto* importCalibrationButton = createButton("导入标定", "导入双目标定文件");
    auto* importImagesButton = createButton("导入图像", "导入左右图像目录");
    connect(importManifestButton, &QToolButton::clicked, this, &RibbonWidget::importManifestRequested);
    connect(importCalibrationButton, &QToolButton::clicked, this, &RibbonWidget::importCalibrationRequested);
    connect(importImagesButton, &QToolButton::clicked, this, &RibbonWidget::importImagesRequested);
    rootLayout->addWidget(createGroup("数据准备区", {importManifestButton, importCalibrationButton, importImagesButton}, this));

    auto* runButton = createButton("开始重建", "执行离线双目重建");
    auto* retryButton = createButton("重试失败帧", "为后续扩展保留入口");
    retryButton->setEnabled(false);
    connect(runButton, &QToolButton::clicked, this, &RibbonWidget::runReconstructionRequested);
    connect(retryButton, &QToolButton::clicked, this, &RibbonWidget::retryFailedRequested);
    rootLayout->addWidget(createGroup("重建处理区", {runButton, retryButton}, this));

    auto* viewHint = createButton("查看结果", "查看左右图像、中心线和点云");
    viewHint->setEnabled(false);
    rootLayout->addWidget(createGroup("结果查看区", {viewHint}, this));

    auto* exportButton = createButton("导出结果", "导出 CSV、PLY 与预览图");
    connect(exportButton, &QToolButton::clicked, this, &RibbonWidget::exportOutputsRequested);
    rootLayout->addWidget(createGroup("结果导出区", {exportButton}, this));

    auto* settingsButton = createButton("系统设置", "调整默认参数与行为");
    connect(settingsButton, &QToolButton::clicked, this, &RibbonWidget::openSettingsRequested);
    rootLayout->addWidget(createGroup("系统区", {settingsButton}, this));
}

QToolButton* RibbonWidget::createButton(const QString& text, const QString& description) {
    auto* button = new QToolButton(this);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setText(text + "\n" + description);
    button->setMinimumWidth(150);
    button->setMinimumHeight(76);
    return button;
}

}  // namespace htmsr::app_shell
