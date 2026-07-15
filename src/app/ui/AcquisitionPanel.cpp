#include "app/ui/AcquisitionPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace htmsr::app {

/*
    函数功能：构造在线采集参数面板，初始化设备、曝光、触发和保存目录等控件
    输入：
        parent：Qt 父控件
    输出：
        无（构造后完成采集面板界面布局和信号连接）
*/
AcquisitionPanel::AcquisitionPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* form = new QFormLayout;

    // 采集面板只维护左右设备、保存目录和一组共享采集参数，首版先覆盖基本闭环。
    leftDeviceCombo_ = new QComboBox;
    rightDeviceCombo_ = new QComboBox;
    useMockProviderCheck_ = new QCheckBox(QString::fromUtf8("使用模拟采集"));
    useMockProviderCheck_->setChecked(true);

    frameCountSpin_ = new QSpinBox;
    frameCountSpin_->setRange(1, 100000);
    frameCountSpin_->setValue(1);

    outputDirectoryEdit_ = new QLineEdit(".");
    auto* outputDirectoryButton = new QPushButton(QString::fromUtf8("..."));
    outputDirectoryButton->setFixedWidth(28);
    auto* outputDirectoryRow = new QWidget;
    auto* outputDirectoryLayout = new QHBoxLayout(outputDirectoryRow);
    outputDirectoryLayout->setContentsMargins(0, 0, 0, 0);
    outputDirectoryLayout->addWidget(outputDirectoryEdit_);
    outputDirectoryLayout->addWidget(outputDirectoryButton);

    exposureTimeSpin_ = new QDoubleSpinBox;
    exposureTimeSpin_->setRange(1.0, 10000000.0);
    exposureTimeSpin_->setDecimals(2);
    exposureTimeSpin_->setValue(5000.0);

    gainSpin_ = new QDoubleSpinBox;
    gainSpin_->setRange(0.0, 48.0);
    gainSpin_->setDecimals(2);
    gainSpin_->setValue(0.0);

    hardwareTriggerCheck_ = new QCheckBox(QString::fromUtf8("硬触发"));
    hardwareTriggerCheck_->setChecked(true);

    triggerLineSpin_ = new QSpinBox;
    triggerLineSpin_->setRange(0, 3);
    triggerLineSpin_->setValue(0);

    timeoutSpin_ = new QSpinBox;
    timeoutSpin_->setRange(100, 60000);
    timeoutSpin_->setValue(1000);

    statusEdit_ = new QLineEdit(QString::fromUtf8("等待采集"));
    statusEdit_->setReadOnly(true);

    form->addRow(QString::fromUtf8("左相机"), leftDeviceCombo_);
    form->addRow(QString::fromUtf8("右相机"), rightDeviceCombo_);
    form->addRow(useMockProviderCheck_);
    form->addRow(QString::fromUtf8("采集帧数"), frameCountSpin_);
    form->addRow(QString::fromUtf8("保存目录"), outputDirectoryRow);
    form->addRow(QString::fromUtf8("曝光 us"), exposureTimeSpin_);
    form->addRow(QString::fromUtf8("增益"), gainSpin_);
    form->addRow(hardwareTriggerCheck_);
    form->addRow(QString::fromUtf8("触发行"), triggerLineSpin_);
    form->addRow(QString::fromUtf8("超时 ms"), timeoutSpin_);
    form->addRow(QString::fromUtf8("状态"), statusEdit_);

    refreshButton_ = new QPushButton(QString::fromUtf8("刷新设备"));
    captureButton_ = new QPushButton(QString::fromUtf8("采集保存"));
    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(refreshButton_);
    buttonLayout->addWidget(captureButton_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addLayout(buttonLayout);
    root->addStretch();

    connect(refreshButton_, &QPushButton::clicked, this, &AcquisitionPanel::refreshDevicesRequested);
    connect(captureButton_, &QPushButton::clicked, this, &AcquisitionPanel::captureRequested);
    connect(outputDirectoryButton, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(this, QString::fromUtf8("选择采集输出目录"), outputDirectoryEdit_->text());
        if (!directory.isEmpty()) {
            outputDirectoryEdit_->setText(directory);
        }
    });
}

/*
    函数功能：将枚举得到的在线设备列表刷新到左右相机下拉框
    输入：
        devices：设备基础信息列表
    输出：
        无（函数会更新左右设备下拉框和状态提示文本）
*/
void AcquisitionPanel::setDevices(const std::vector<CameraDeviceInfo>& devices)
{
    leftDeviceCombo_->clear();
    rightDeviceCombo_->clear();

    for (const auto& device : devices) {
        const QString label = QString::fromStdString(device.name + " [" + device.transportType + " " + device.serialNumber + "]");
        const QString id = QString::fromStdString(device.id);
        leftDeviceCombo_->addItem(label, id);
        rightDeviceCombo_->addItem(label, id);
    }

    if (rightDeviceCombo_->count() > 1) {
        rightDeviceCombo_->setCurrentIndex(1);
    }

    setStatusText(devices.empty()
        ? QString::fromUtf8("未枚举到真实相机，可使用模拟采集")
        : QString::fromUtf8("已枚举到 %1 台相机").arg(static_cast<qulonglong>(devices.size())));
}

/*
    函数功能：从采集面板读取一次双相机采集任务配置
    输入：
        无
    输出：
        返回值：包含左右设备 id、帧数、输出目录、曝光、增益和触发参数的采集配置
*/
StereoCameraConfig AcquisitionPanel::stereoCameraConfig() const
{
    StereoCameraConfig config;
    config.leftDeviceId = leftDeviceCombo_->currentData().toString().toStdString();
    config.rightDeviceId = rightDeviceCombo_->currentData().toString().toStdString();
    config.useMockProvider = useMockProviderCheck_->isChecked();
    config.frameCount = frameCountSpin_->value();
    config.outputDirectory = outputDirectoryEdit_->text().toStdString();
    config.leftParameters.exposureTime = exposureTimeSpin_->value();
    config.rightParameters.exposureTime = exposureTimeSpin_->value();
    config.leftParameters.gain = gainSpin_->value();
    config.rightParameters.gain = gainSpin_->value();
    config.leftParameters.useHardwareTrigger = hardwareTriggerCheck_->isChecked();
    config.rightParameters.useHardwareTrigger = hardwareTriggerCheck_->isChecked();
    config.leftParameters.triggerSourceLine = triggerLineSpin_->value();
    config.rightParameters.triggerSourceLine = triggerLineSpin_->value();
    config.leftParameters.grabTimeoutMs = timeoutSpin_->value();
    config.rightParameters.grabTimeoutMs = timeoutSpin_->value();
    return config;
}

/*
    函数功能：设置采集面板忙碌状态
    输入：
        busy：是否正在执行在线采集任务
    输出：
        无（函数会启用或禁用刷新与采集按钮）
*/
void AcquisitionPanel::setBusy(bool busy)
{
    refreshButton_->setEnabled(!busy);
    captureButton_->setEnabled(!busy);
}

// 状态文本统一走只读输入框显示，便于向用户反馈当前枚举和采集结果。
void AcquisitionPanel::setStatusText(const QString& text)
{
    statusEdit_->setText(text);
}

} // namespace htmsr::app
