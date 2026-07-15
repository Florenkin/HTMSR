#include "app/ui/AcquisitionPanel.h"

#include "app/acquisition/GalvoController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace htmsr::app {
namespace {

QWidget* wrapPathRow(QLineEdit* edit, QPushButton* button)
{
    auto* rowWidget = new QWidget;
    auto* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(edit);
    rowLayout->addWidget(button);
    return rowWidget;
}

} // namespace

/*
    函数功能：构造在线采集参数面板，初始化设备、曝光、触发、振镜参数和保存目录等控件
    输入：
        parent：Qt 父控件
    输出：
        无（构造后完成采集面板界面布局和信号连接）
*/
AcquisitionPanel::AcquisitionPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* cameraGroup = new QGroupBox(QString::fromUtf8("相机采集"));
    auto* cameraForm = new QFormLayout(cameraGroup);

    leftDeviceCombo_ = new QComboBox;
    rightDeviceCombo_ = new QComboBox;
    useMockProviderCheck_ = new QCheckBox(QString::fromUtf8("使用模拟采集"));
    useMockProviderCheck_->setChecked(false);

    frameCountSpin_ = new QSpinBox;
    frameCountSpin_->setRange(1, 100000);
    frameCountSpin_->setValue(100);

    outputDirectoryEdit_ = new QLineEdit(".");
    auto* outputDirectoryButton = new QPushButton(QString::fromUtf8("..."));
    outputDirectoryButton->setFixedWidth(28);

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

    cameraForm->addRow(QString::fromUtf8("左相机"), leftDeviceCombo_);
    cameraForm->addRow(QString::fromUtf8("右相机"), rightDeviceCombo_);
    cameraForm->addRow(useMockProviderCheck_);
    cameraForm->addRow(QString::fromUtf8("采集帧数"), frameCountSpin_);
    cameraForm->addRow(QString::fromUtf8("保存目录"), wrapPathRow(outputDirectoryEdit_, outputDirectoryButton));
    cameraForm->addRow(QString::fromUtf8("曝光 us"), exposureTimeSpin_);
    cameraForm->addRow(QString::fromUtf8("增益"), gainSpin_);
    cameraForm->addRow(hardwareTriggerCheck_);
    cameraForm->addRow(QString::fromUtf8("触发线"), triggerLineSpin_);
    cameraForm->addRow(QString::fromUtf8("超时 ms"), timeoutSpin_);

    auto* galvoGroup = new QGroupBox(QString::fromUtf8("振镜控制"));
    auto* galvoForm = new QFormLayout(galvoGroup);

    galvoPortEdit_ = new QLineEdit("COM3");
    const auto ports = enumerateSerialPortNames();
    if (!ports.empty()) {
        galvoPortEdit_->setText(QString::fromStdString(ports.front()));
    }

    galvoBaudRateSpin_ = new QSpinBox;
    galvoBaudRateSpin_->setRange(1200, 921600);
    galvoBaudRateSpin_->setValue(115200);

    galvoCommandTimeoutSpin_ = new QSpinBox;
    galvoCommandTimeoutSpin_->setRange(50, 10000);
    galvoCommandTimeoutSpin_->setValue(500);

    galvoSyncModeCombo_ = new QComboBox;
    galvoSyncModeCombo_->addItems({ QString::fromUtf8("同步"), QString::fromUtf8("异步") });
    galvoSyncModeCombo_->setCurrentIndex(0);

    galvoDirectionCombo_ = new QComboBox;
    galvoDirectionCombo_->addItems({ QString::fromUtf8("正向"), QString::fromUtf8("反向") });
    galvoDirectionCombo_->setCurrentIndex(0);

    galvoCaptureIntervalSpin_ = new QSpinBox;
    galvoCaptureIntervalSpin_->setRange(1, 255);
    galvoCaptureIntervalSpin_->setValue(10);

    galvoContinuousWaitSpin_ = new QSpinBox;
    galvoContinuousWaitSpin_->setRange(1, 255);
    galvoContinuousWaitSpin_->setValue(20);

    galvoStepAngleSpin_ = new QDoubleSpinBox;
    galvoStepAngleSpin_->setRange(0.01, 650.25);
    galvoStepAngleSpin_->setDecimals(4);
    galvoStepAngleSpin_->setValue(0.02);

    galvoAutoRotationAngleSpin_ = new QSpinBox;
    galvoAutoRotationAngleSpin_->setRange(0, 40);
    galvoAutoRotationAngleSpin_->setValue(22);

    galvoForwardSpeedSpin_ = new QSpinBox;
    galvoForwardSpeedSpin_->setRange(1, 1000);
    galvoForwardSpeedSpin_->setValue(10);

    galvoReverseSpeedSpin_ = new QSpinBox;
    galvoReverseSpeedSpin_->setRange(1, 1000);
    galvoReverseSpeedSpin_->setValue(10);

    galvoLaserDutySpin_ = new QSpinBox;
    galvoLaserDutySpin_->setRange(10, 310);
    galvoLaserDutySpin_->setValue(100);

    galvoVoltageRangeSpin_ = new QDoubleSpinBox;
    galvoVoltageRangeSpin_->setRange(0.0, 25.5);
    galvoVoltageRangeSpin_->setDecimals(1);
    galvoVoltageRangeSpin_->setValue(7.0);

    forceRecalibrationCheck_ = new QCheckBox(QString::fromUtf8("扫描前强制重标"));
    forceRecalibrationCheck_->setChecked(false);

    galvoForm->addRow(QString::fromUtf8("串口号"), galvoPortEdit_);
    galvoForm->addRow(QString::fromUtf8("波特率"), galvoBaudRateSpin_);
    galvoForm->addRow(QString::fromUtf8("命令超时 ms"), galvoCommandTimeoutSpin_);
    galvoForm->addRow(QString::fromUtf8("同步模式"), galvoSyncModeCombo_);
    galvoForm->addRow(QString::fromUtf8("扫描方向"), galvoDirectionCombo_);
    galvoForm->addRow(QString::fromUtf8("抓图间隔 ms"), galvoCaptureIntervalSpin_);
    galvoForm->addRow(QString::fromUtf8("连续模式等待 ms"), galvoContinuousWaitSpin_);
    galvoForm->addRow(QString::fromUtf8("步进角度 °"), galvoStepAngleSpin_);
    galvoForm->addRow(QString::fromUtf8("自动旋转角度 °"), galvoAutoRotationAngleSpin_);
    galvoForm->addRow(QString::fromUtf8("正向速度 ms"), galvoForwardSpeedSpin_);
    galvoForm->addRow(QString::fromUtf8("反向速度 ms"), galvoReverseSpeedSpin_);
    galvoForm->addRow(QString::fromUtf8("激光占空比"), galvoLaserDutySpin_);
    galvoForm->addRow(QString::fromUtf8("电压范围 V"), galvoVoltageRangeSpin_);
    galvoForm->addRow(forceRecalibrationCheck_);

    statusEdit_ = new QLineEdit(QString::fromUtf8("等待采集"));
    statusEdit_->setReadOnly(true);
    summaryEdit_ = new QLineEdit(QString::fromUtf8("尚未执行自动流程"));
    summaryEdit_->setReadOnly(true);

    refreshButton_ = new QPushButton(QString::fromUtf8("刷新设备"));
    captureButton_ = new QPushButton(QString::fromUtf8("采集保存"));
    autoCalibrationButton_ = new QPushButton(QString::fromUtf8("一键自动标定"));
    scanReconstructButton_ = new QPushButton(QString::fromUtf8("一键扫描重建"));

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(refreshButton_);
    buttonLayout->addWidget(captureButton_);
    buttonLayout->addWidget(autoCalibrationButton_);
    buttonLayout->addWidget(scanReconstructButton_);

    auto* statusGroup = new QGroupBox(QString::fromUtf8("任务状态"));
    auto* statusForm = new QFormLayout(statusGroup);
    statusForm->addRow(QString::fromUtf8("状态"), statusEdit_);
    statusForm->addRow(QString::fromUtf8("结果摘要"), summaryEdit_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(cameraGroup);
    root->addWidget(galvoGroup);
    root->addWidget(statusGroup);
    root->addLayout(buttonLayout);
    root->addStretch();

    connect(refreshButton_, &QPushButton::clicked, this, &AcquisitionPanel::refreshDevicesRequested);
    connect(captureButton_, &QPushButton::clicked, this, &AcquisitionPanel::captureRequested);
    connect(autoCalibrationButton_, &QPushButton::clicked, this, &AcquisitionPanel::autoCalibrationRequested);
    connect(scanReconstructButton_, &QPushButton::clicked, this, &AcquisitionPanel::scanAndReconstructRequested);
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
        ? QString::fromUtf8("未枚举到真实相机，可使用采集保存调试")
        : QString::fromUtf8("已枚举到 %1 台相机").arg(static_cast<qulonglong>(devices.size())));
}

/*
    函数功能：从采集面板读取一次双相机采集任务配置
    输入：
        无
    输出：
        返回值：双相机 id、帧数、输出目录、曝光、增益和触发配置
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
    函数功能：从采集面板读取联动扫描配置
    输入：
        无
    输出：
        返回值：包含双相机采集参数、振镜串口参数和重标开关的联动配置
*/
IntegratedScanConfig AcquisitionPanel::integratedScanConfig() const
{
    IntegratedScanConfig config;
    config.stereoCamera = stereoCameraConfig();
    config.galvo.portName = galvoPortEdit_->text().toStdString();
    config.galvo.baudRate = galvoBaudRateSpin_->value();
    config.galvo.commandTimeoutMs = galvoCommandTimeoutSpin_->value();
    config.galvo.syncMode = galvoSyncModeCombo_->currentIndex() == 0 ? GalvoSyncMode::Sync : GalvoSyncMode::Async;
    config.galvo.direction = galvoDirectionCombo_->currentIndex() == 0 ? GalvoScanDirection::Forward : GalvoScanDirection::Reverse;
    config.galvo.captureIntervalMs = galvoCaptureIntervalSpin_->value();
    config.galvo.continuousCaptureWaitMs = galvoContinuousWaitSpin_->value();
    config.galvo.stepAngleDeg = galvoStepAngleSpin_->value();
    config.galvo.autoRotationAngleDeg = galvoAutoRotationAngleSpin_->value();
    config.galvo.forwardSpeedMs = galvoForwardSpeedSpin_->value();
    config.galvo.reverseSpeedMs = galvoReverseSpeedSpin_->value();
    config.galvo.laserDuty = galvoLaserDutySpin_->value();
    config.galvo.voltageRangeV = galvoVoltageRangeSpin_->value();
    config.forceRecalibration = forceRecalibrationCheck_->isChecked();
    return config;
}

/*
    函数功能：设置采集面板忙碌状态
    输入：
        busy：是否正在执行采集任务
    输出：
        无（函数会启用或禁用关键按钮）
*/
void AcquisitionPanel::setBusy(bool busy)
{
    refreshButton_->setEnabled(!busy);
    captureButton_->setEnabled(!busy);
    autoCalibrationButton_->setEnabled(!busy);
    scanReconstructButton_->setEnabled(!busy);
}

// 状态文本统一走只读输入框显示，便于向用户反馈当前枚举和采集结果。
void AcquisitionPanel::setStatusText(const QString& text)
{
    statusEdit_->setText(text);
}

// 结果摘要用于汇总一键流程的最终产物，例如 RMS、点云点数和复用标定状态。
void AcquisitionPanel::setResultSummary(const QString& text)
{
    summaryEdit_->setText(text);
}

} // namespace htmsr::app
