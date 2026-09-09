#include "app/ui/AcquisitionPanel.h"

#include "app/acquisition/GalvoController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
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

std::string textOf(const QLineEdit* edit)
{
    return edit->text().toStdString();
}

cv::Rect makeRect(const QSpinBox* x, const QSpinBox* y, const QSpinBox* w, const QSpinBox* h)
{
    return cv::Rect(x->value(), y->value(), w->value(), h->value());
}

QScrollArea* makeScrollArea(QWidget* content)
{
    auto* scrollArea = new QScrollArea;
    scrollArea->setWidget(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    return scrollArea;
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
    frameCountSpin_->setValue(200);

    outputDirectoryEdit_ = new QLineEdit(".");
    auto* outputDirectoryButton = new QPushButton(QString::fromUtf8("..."));
    outputDirectoryButton->setFixedWidth(28);

    exposureTimeSpin_ = new QDoubleSpinBox;
    exposureTimeSpin_->setRange(1.0, 10000000.0);
    exposureTimeSpin_->setDecimals(2);
    exposureTimeSpin_->setValue(500000.0);

    gainSpin_ = new QDoubleSpinBox;
    gainSpin_->setRange(0.0, 48.0);
    gainSpin_->setDecimals(2);
    gainSpin_->setValue(15.0);

    hardwareTriggerCheck_ = new QCheckBox(QString::fromUtf8("硬触发"));
    hardwareTriggerCheck_->setChecked(false);

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

    auto* calibrationGroup = new QGroupBox(QString::fromUtf8("相机在线标定"));
    auto* calibrationForm = new QFormLayout(calibrationGroup);
    leftCalibrationEdit_ = createPathRow(calibrationForm, QString::fromUtf8("左标定目录"), true);
    rightCalibrationEdit_ = createPathRow(calibrationForm, QString::fromUtf8("右标定目录"), true);
    calibrationFileEdit_ = createPathRow(calibrationForm, QString::fromUtf8("标定文件"), false);
    boardWidthSpin_ = createSpinRow(2, 100, 11);
    boardHeightSpin_ = createSpinRow(2, 100, 8);
    squareWidthSpin_ = createDoubleSpinRow(0.001, 10000.0, 15.0);
    squareHeightSpin_ = createDoubleSpinRow(0.001, 10000.0, 15.0);
    imageBeginSpin_ = createSpinRow(-1, 100000, -1);
    imageEndSpin_ = createSpinRow(-1, 100000, -1);
    calibrationForm->addRow(QString::fromUtf8("棋盘格宽"), boardWidthSpin_);
    calibrationForm->addRow(QString::fromUtf8("棋盘格高"), boardHeightSpin_);
    calibrationForm->addRow(QString::fromUtf8("方格宽"), squareWidthSpin_);
    calibrationForm->addRow(QString::fromUtf8("方格高"), squareHeightSpin_);
    calibrationForm->addRow(QString::fromUtf8("起始索引"), imageBeginSpin_);
    calibrationForm->addRow(QString::fromUtf8("结束索引"), imageEndSpin_);

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
    galvoCaptureIntervalSpin_->setValue(30);

    galvoContinuousWaitSpin_ = new QSpinBox;
    galvoContinuousWaitSpin_->setRange(1, 255);
    galvoContinuousWaitSpin_->setValue(50);

    galvoTotalRotationAngleSpin_ = new QDoubleSpinBox;
    galvoTotalRotationAngleSpin_->setRange(0.01, 650.25);
    galvoTotalRotationAngleSpin_->setDecimals(4);
    galvoTotalRotationAngleSpin_->setValue(10.0);

    galvoStepAngleSpin_ = new QDoubleSpinBox;
    galvoStepAngleSpin_->setRange(0.01, 650.25);
    galvoStepAngleSpin_->setDecimals(4);
    galvoStepAngleSpin_->setValue(0.05);

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

    galvoCenterButton_ = new QPushButton(QString::fromUtf8("激光回中心"));

    rawGalvoCommandEdit_ = new QLineEdit(QString::fromUtf8("55 AA 01 1A 1A"));
    rawGalvoCommandButton_ = new QPushButton(QString::fromUtf8("发送指令"));

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
    galvoForm->addRow(QString::fromUtf8("总旋转角度 °"), galvoTotalRotationAngleSpin_);
    galvoForm->addRow(QString::fromUtf8("步进角度 °"), galvoStepAngleSpin_);
    galvoForm->addRow(QString::fromUtf8("自动旋转角度 °"), galvoAutoRotationAngleSpin_);
    galvoForm->addRow(QString::fromUtf8("正向速度 ms"), galvoForwardSpeedSpin_);
    galvoForm->addRow(QString::fromUtf8("反向速度 ms"), galvoReverseSpeedSpin_);
    galvoForm->addRow(QString::fromUtf8("激光占空比"), galvoLaserDutySpin_);

    auto* rawGalvoCommandLayout = new QHBoxLayout;
    rawGalvoCommandLayout->setContentsMargins(0, 0, 0, 0);
    rawGalvoCommandLayout->addWidget(rawGalvoCommandEdit_, 1);
    rawGalvoCommandLayout->addWidget(rawGalvoCommandButton_);
    auto* rawGalvoCommandWidget = new QWidget;
    rawGalvoCommandWidget->setLayout(rawGalvoCommandLayout);

    galvoForm->addRow(QString::fromUtf8("方向归中"), galvoCenterButton_);
    galvoForm->addRow(QString::fromUtf8("电压范围 V"), galvoVoltageRangeSpin_);
    galvoForm->addRow(QString::fromUtf8("相机指令"), rawGalvoCommandWidget);
    galvoForm->addRow(forceRecalibrationCheck_);

    auto* reconstructionGroup = new QGroupBox(QString::fromUtf8("在线重建"));
    auto* reconstructionForm = new QFormLayout(reconstructionGroup);
    leftReconstructionEdit_ = createPathRow(reconstructionForm, QString::fromUtf8("左重建目录"), true);
    rightReconstructionEdit_ = createPathRow(reconstructionForm, QString::fromUtf8("右重建目录"), true);
    laserModeCombo_ = new QComboBox;
    laserModeCombo_->addItems({ QString::fromUtf8("灰度重心"), QString::fromUtf8("Steger") });
    laserModeCombo_->setCurrentIndex(0);
    laserColorCombo_ = new QComboBox;
    laserColorCombo_->addItems({ QString::fromUtf8("蓝色"), QString::fromUtf8("绿色"), QString::fromUtf8("红色"), QString::fromUtf8("灰度") });
    laserColorCombo_->setCurrentIndex(3);
    const LaserExtractionConfig defaultLaserConfig;
    leftRoiXSpin_ = createSpinRow(0, 100000, defaultLaserConfig.leftRoi.x);
    leftRoiYSpin_ = createSpinRow(0, 100000, defaultLaserConfig.leftRoi.y);
    leftRoiWSpin_ = createSpinRow(1, 100000, defaultLaserConfig.leftRoi.width);
    leftRoiHSpin_ = createSpinRow(1, 100000, defaultLaserConfig.leftRoi.height);
    rightRoiXSpin_ = createSpinRow(0, 100000, defaultLaserConfig.rightRoi.x);
    rightRoiYSpin_ = createSpinRow(0, 100000, defaultLaserConfig.rightRoi.y);
    rightRoiWSpin_ = createSpinRow(1, 100000, defaultLaserConfig.rightRoi.width);
    rightRoiHSpin_ = createSpinRow(1, 100000, defaultLaserConfig.rightRoi.height);
    grayThresholdSpin_ = createSpinRow(0, 255, 120);
    minGraySpin_ = createSpinRow(0, 255, 20);
    binaryThresholdSpin_ = createDoubleSpinRow(0.0, 255.0, 100.0);
    selectionThresholdSpin_ = createDoubleSpinRow(0.0, 255.0, 200.0);
    stripeWidthSpin_ = createDoubleSpinRow(0.5, 100.0, 5.0);
    matchDistanceSpin_ = createDoubleSpinRow(0.0001, 100.0, 0.5);
    removeEndpointsCheck_ = new QCheckBox(QString::fromUtf8("剔除端点"));
    removeEndpointCountSpin_ = createSpinRow(0, 10000, 10);

    auto* leftRoiLayout = new QHBoxLayout;
    leftRoiLayout->addWidget(leftRoiXSpin_);
    leftRoiLayout->addWidget(leftRoiYSpin_);
    leftRoiLayout->addWidget(leftRoiWSpin_);
    leftRoiLayout->addWidget(leftRoiHSpin_);
    auto* rightRoiLayout = new QHBoxLayout;
    rightRoiLayout->addWidget(rightRoiXSpin_);
    rightRoiLayout->addWidget(rightRoiYSpin_);
    rightRoiLayout->addWidget(rightRoiWSpin_);
    rightRoiLayout->addWidget(rightRoiHSpin_);

    reconstructionForm->addRow(QString::fromUtf8("算法"), laserModeCombo_);
    reconstructionForm->addRow(QString::fromUtf8("颜色"), laserColorCombo_);
    reconstructionForm->addRow(QString::fromUtf8("左ROI x/y/w/h"), leftRoiLayout);
    reconstructionForm->addRow(QString::fromUtf8("右ROI x/y/w/h"), rightRoiLayout);
    reconstructionForm->addRow(QString::fromUtf8("灰度阈值"), grayThresholdSpin_);
    reconstructionForm->addRow(QString::fromUtf8("最小灰度"), minGraySpin_);
    reconstructionForm->addRow(QString::fromUtf8("二值阈值"), binaryThresholdSpin_);
    reconstructionForm->addRow(QString::fromUtf8("筛选阈值"), selectionThresholdSpin_);
    reconstructionForm->addRow(QString::fromUtf8("线宽"), stripeWidthSpin_);
    reconstructionForm->addRow(QString::fromUtf8("匹配距离"), matchDistanceSpin_);
    reconstructionForm->addRow(removeEndpointsCheck_);
    reconstructionForm->addRow(QString::fromUtf8("端点数量"), removeEndpointCountSpin_);

    statusEdit_ = new QLineEdit(QString::fromUtf8("等待采集"));
    statusEdit_->setReadOnly(true);
    summaryEdit_ = new QLineEdit(QString::fromUtf8("尚未执行自动流程"));
    summaryEdit_->setReadOnly(true);

    refreshButton_ = new QPushButton(QString::fromUtf8("刷新设备"));
    startCalibrationCaptureButton_ = new QPushButton(QString::fromUtf8("开始标定采集"));
    captureCalibrationFrameButton_ = new QPushButton(QString::fromUtf8("采集当前帧"));
    calibrateCapturedFramesButton_ = new QPushButton(QString::fromUtf8("标定当前采集帧"));
    saveCalibrationResultButton_ = new QPushButton(QString::fromUtf8("保存标定结果"));
    loadCalibrationResultButton_ = new QPushButton(QString::fromUtf8("加载标定结果"));
    finishCalibrationCaptureButton_ = new QPushButton(QString::fromUtf8("结束标定采集"));
    startReconstructionCaptureButton_ = new QPushButton(QString::fromUtf8("开始重建采集"));
    reconstructCapturedFramesButton_ = new QPushButton(QString::fromUtf8("重建当前采集帧"));

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(refreshButton_);

    auto* calibrationButtonLayout = new QHBoxLayout;
    calibrationButtonLayout->addWidget(startCalibrationCaptureButton_);
    calibrationButtonLayout->addWidget(captureCalibrationFrameButton_);
    calibrationButtonLayout->addWidget(calibrateCapturedFramesButton_);
    calibrationButtonLayout->addWidget(finishCalibrationCaptureButton_);

    auto* calibrationResultButtonLayout = new QHBoxLayout;
    calibrationResultButtonLayout->addWidget(saveCalibrationResultButton_);
    calibrationResultButtonLayout->addWidget(loadCalibrationResultButton_);

    auto* reconstructionButtonLayout = new QHBoxLayout;
    reconstructionButtonLayout->addWidget(startReconstructionCaptureButton_);
    reconstructionButtonLayout->addWidget(reconstructCapturedFramesButton_);

    auto* statusGroup = new QGroupBox(QString::fromUtf8("任务状态"));
    auto* statusForm = new QFormLayout(statusGroup);
    statusForm->addRow(QString::fromUtf8("状态"), statusEdit_);
    statusForm->addRow(QString::fromUtf8("结果摘要"), summaryEdit_);

    auto* calibrationPage = new QWidget;
    auto* calibrationPageLayout = new QVBoxLayout(calibrationPage);
    calibrationPageLayout->addWidget(calibrationGroup);
    calibrationPageLayout->addLayout(calibrationButtonLayout);
    calibrationPageLayout->addLayout(calibrationResultButtonLayout);
    calibrationPageLayout->addStretch();

    auto* reconstructionPage = new QWidget;
    auto* reconstructionPageLayout = new QVBoxLayout(reconstructionPage);
    reconstructionPageLayout->addWidget(galvoGroup);
    reconstructionPageLayout->addWidget(reconstructionGroup);
    reconstructionPageLayout->addLayout(reconstructionButtonLayout);
    reconstructionPageLayout->addStretch();

    auto* workflowTabs = new QTabWidget;
    workflowTabs->addTab(makeScrollArea(calibrationPage), QString::fromUtf8("相机在线标定"));
    workflowTabs->addTab(makeScrollArea(reconstructionPage), QString::fromUtf8("在线重建"));

    auto* root = new QVBoxLayout(this);
    root->addWidget(cameraGroup);
    root->addLayout(buttonLayout);
    root->addWidget(workflowTabs);
    root->addWidget(statusGroup);
    root->addStretch();

    connect(refreshButton_, &QPushButton::clicked, this, &AcquisitionPanel::refreshDevicesRequested);
    connect(startCalibrationCaptureButton_, &QPushButton::clicked, this, &AcquisitionPanel::startCalibrationCaptureRequested);
    connect(captureCalibrationFrameButton_, &QPushButton::clicked, this, &AcquisitionPanel::captureCalibrationFrameRequested);
    connect(calibrateCapturedFramesButton_, &QPushButton::clicked, this, &AcquisitionPanel::calibrateCapturedFramesRequested);
    connect(saveCalibrationResultButton_, &QPushButton::clicked, this, &AcquisitionPanel::saveCalibrationResultRequested);
    connect(loadCalibrationResultButton_, &QPushButton::clicked, this, &AcquisitionPanel::loadCalibrationResultRequested);
    connect(finishCalibrationCaptureButton_, &QPushButton::clicked, this, &AcquisitionPanel::finishCalibrationCaptureRequested);
    connect(startReconstructionCaptureButton_, &QPushButton::clicked, this, &AcquisitionPanel::startReconstructionCaptureRequested);
    connect(reconstructCapturedFramesButton_, &QPushButton::clicked, this, &AcquisitionPanel::reconstructCapturedFramesRequested);
    connect(galvoCenterButton_, &QPushButton::clicked, this, &AcquisitionPanel::returnGalvoCenterRequested);
    auto sendRawGalvoCommand = [this]() {
        emit sendRawGalvoCommandRequested(rawGalvoCommandEdit_->text());
    };
    connect(rawGalvoCommandButton_, &QPushButton::clicked, this, sendRawGalvoCommand);
    connect(rawGalvoCommandEdit_, &QLineEdit::returnPressed, this, sendRawGalvoCommand);
    connect(galvoPortEdit_, &QLineEdit::textChanged, this, [this]() { emit galvoConfigChanged(); });
    connect(galvoBaudRateSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoCommandTimeoutSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoSyncModeCombo_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoDirectionCombo_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoCaptureIntervalSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoContinuousWaitSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoTotalRotationAngleSpin_, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double) { emit galvoConfigChanged(); });
    connect(galvoStepAngleSpin_, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double) { emit galvoConfigChanged(); });
    connect(galvoAutoRotationAngleSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoForwardSpeedSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoReverseSpeedSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoLaserDutySpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoVoltageRangeSpin_, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double) { emit galvoConfigChanged(); });
    connect(outputDirectoryButton, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(this, QString::fromUtf8("选择采集输出目录"), outputDirectoryEdit_->text());
        if (!directory.isEmpty()) {
            outputDirectoryEdit_->setText(directory);
        }
    });
    updateActionButtons();
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
        ? QString::fromUtf8("未枚举到真实相机，可勾选模拟采集进行会话流程测试")
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
    config.totalRotationAngleDeg = galvoTotalRotationAngleSpin_->value();
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

AppProjectConfig AcquisitionPanel::projectConfig() const
{
    AppProjectConfig config;
    config.leftCalibrationDirectory = textOf(leftCalibrationEdit_);
    config.rightCalibrationDirectory = textOf(rightCalibrationEdit_);
    config.leftReconstructionDirectory = textOf(leftReconstructionEdit_);
    config.rightReconstructionDirectory = textOf(rightReconstructionEdit_);
    config.calibrationFile = textOf(calibrationFileEdit_);
    config.outputDirectory = textOf(outputDirectoryEdit_);
    config.calibrationInput = calibrationInput();
    config.laserConfig = reconstructionInput({}).laserConfig;
    config.matchDistanceThreshold = matchDistanceSpin_->value();
    return config;
}

CalibrationInput AcquisitionPanel::calibrationInput() const
{
    CalibrationInput input;
    input.leftDirectory = textOf(leftCalibrationEdit_);
    input.rightDirectory = textOf(rightCalibrationEdit_);
    input.boardSize = cv::Size(boardWidthSpin_->value(), boardHeightSpin_->value());
    input.squareSize = cv::Size2d(squareWidthSpin_->value(), squareHeightSpin_->value());
    input.imageRange = { imageBeginSpin_->value(), imageEndSpin_->value() };
    input.outputFile = textOf(calibrationFileEdit_);
    return input;
}

ReconstructionInput AcquisitionPanel::reconstructionInput(const CalibrationResult& calibration) const
{
    ReconstructionInput input;
    input.leftDirectory = textOf(leftReconstructionEdit_);
    input.rightDirectory = textOf(rightReconstructionEdit_);
    input.imageRange = { imageBeginSpin_->value(), imageEndSpin_->value() };
    input.calibration = calibration;
    input.laserConfig.mode = laserModeCombo_->currentIndex() == 1 ? LaserExtractionMode::Steger : LaserExtractionMode::GrayCentroid;
    input.laserConfig.laserColor = static_cast<LaserColor>(laserColorCombo_->currentIndex() == 0 ? 2 : laserColorCombo_->currentIndex() == 1 ? 1 : laserColorCombo_->currentIndex() == 2 ? 0 : 3);
    input.laserConfig.leftRoi = makeRect(leftRoiXSpin_, leftRoiYSpin_, leftRoiWSpin_, leftRoiHSpin_);
    input.laserConfig.rightRoi = makeRect(rightRoiXSpin_, rightRoiYSpin_, rightRoiWSpin_, rightRoiHSpin_);
    input.laserConfig.grayThreshold = grayThresholdSpin_->value();
    input.laserConfig.minGray = minGraySpin_->value();
    input.laserConfig.binaryThreshold = binaryThresholdSpin_->value();
    input.laserConfig.selectionThreshold = selectionThresholdSpin_->value();
    input.laserConfig.stripeWidth = stripeWidthSpin_->value();
    input.laserConfig.removeEndPoints = removeEndpointsCheck_->isChecked();
    input.laserConfig.removeEndPointCount = removeEndpointCountSpin_->value();
    input.matchDistanceThreshold = matchDistanceSpin_->value();
    return input;
}

void AcquisitionPanel::setProjectConfig(const AppProjectConfig& config)
{
    leftCalibrationEdit_->setText(QString::fromStdString(config.leftCalibrationDirectory));
    rightCalibrationEdit_->setText(QString::fromStdString(config.rightCalibrationDirectory));
    leftReconstructionEdit_->setText(QString::fromStdString(config.leftReconstructionDirectory));
    rightReconstructionEdit_->setText(QString::fromStdString(config.rightReconstructionDirectory));
    calibrationFileEdit_->setText(QString::fromStdString(config.calibrationFile));
    outputDirectoryEdit_->setText(QString::fromStdString(config.outputDirectory));
    boardWidthSpin_->setValue(config.calibrationInput.boardSize.width);
    boardHeightSpin_->setValue(config.calibrationInput.boardSize.height);
    squareWidthSpin_->setValue(config.calibrationInput.squareSize.width);
    squareHeightSpin_->setValue(config.calibrationInput.squareSize.height);
    imageBeginSpin_->setValue(config.calibrationInput.imageRange.begin);
    imageEndSpin_->setValue(config.calibrationInput.imageRange.end);
    laserModeCombo_->setCurrentIndex(config.laserConfig.mode == LaserExtractionMode::Steger ? 1 : 0);
    const int colorIndex = config.laserConfig.laserColor == LaserColor::Blue ? 0
        : config.laserConfig.laserColor == LaserColor::Green ? 1
        : config.laserConfig.laserColor == LaserColor::Red ? 2
        : 3;
    laserColorCombo_->setCurrentIndex(colorIndex);
    leftRoiXSpin_->setValue(config.laserConfig.leftRoi.x);
    leftRoiYSpin_->setValue(config.laserConfig.leftRoi.y);
    leftRoiWSpin_->setValue(config.laserConfig.leftRoi.width);
    leftRoiHSpin_->setValue(config.laserConfig.leftRoi.height);
    rightRoiXSpin_->setValue(config.laserConfig.rightRoi.x);
    rightRoiYSpin_->setValue(config.laserConfig.rightRoi.y);
    rightRoiWSpin_->setValue(config.laserConfig.rightRoi.width);
    rightRoiHSpin_->setValue(config.laserConfig.rightRoi.height);
    grayThresholdSpin_->setValue(config.laserConfig.grayThreshold);
    minGraySpin_->setValue(config.laserConfig.minGray);
    binaryThresholdSpin_->setValue(config.laserConfig.binaryThreshold);
    selectionThresholdSpin_->setValue(config.laserConfig.selectionThreshold);
    stripeWidthSpin_->setValue(config.laserConfig.stripeWidth);
    matchDistanceSpin_->setValue(config.matchDistanceThreshold);
    removeEndpointsCheck_->setChecked(config.laserConfig.removeEndPoints);
    removeEndpointCountSpin_->setValue(config.laserConfig.removeEndPointCount);
}

void AcquisitionPanel::setCalibrationDirectories(const std::string& leftDirectory, const std::string& rightDirectory)
{
    leftCalibrationEdit_->setText(QString::fromStdString(leftDirectory));
    rightCalibrationEdit_->setText(QString::fromStdString(rightDirectory));
}

void AcquisitionPanel::setCalibrationFile(const std::string& calibrationFile)
{
    calibrationFileEdit_->setText(QString::fromStdString(calibrationFile));
}

void AcquisitionPanel::setReconstructionDirectories(const std::string& leftDirectory, const std::string& rightDirectory)
{
    leftReconstructionEdit_->setText(QString::fromStdString(leftDirectory));
    rightReconstructionEdit_->setText(QString::fromStdString(rightDirectory));
}

QLineEdit* AcquisitionPanel::createPathRow(QFormLayout* form, const QString& label, bool directory)
{
    auto* edit = new QLineEdit;
    auto* button = new QPushButton(QString::fromUtf8("..."));
    button->setFixedWidth(28);
    connect(button, &QPushButton::clicked, this, [edit, directory, this]() {
        const QString value = directory
            ? QFileDialog::getExistingDirectory(this, QString::fromUtf8("选择目录"), edit->text())
            : QFileDialog::getSaveFileName(this, QString::fromUtf8("选择文件"), edit->text(), QString::fromUtf8("YAML (*.yml *.yaml);;所有文件 (*.*)"));
        if (!value.isEmpty()) {
            edit->setText(value);
        }
    });

    form->addRow(label, wrapPathRow(edit, button));
    return edit;
}

QSpinBox* AcquisitionPanel::createSpinRow(int min, int max, int value)
{
    auto* spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setValue(value);
    return spin;
}

QDoubleSpinBox* AcquisitionPanel::createDoubleSpinRow(double min, double max, double value)
{
    auto* spin = new QDoubleSpinBox;
    spin->setRange(min, max);
    spin->setDecimals(4);
    spin->setValue(value);
    return spin;
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
    busy_ = busy;
    updateActionButtons();
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

void AcquisitionPanel::setCalibrationCaptureState(bool active, int capturedFrameCount)
{
    calibrationCaptureActive_ = active;
    calibrationCapturedFrameCount_ = capturedFrameCount;
    updateActionButtons();
}

void AcquisitionPanel::setReconstructionCaptureReady(bool ready)
{
    reconstructionCaptureReady_ = ready;
    updateActionButtons();
}

void AcquisitionPanel::updateActionButtons()
{
    const bool idle = !busy_;
    refreshButton_->setEnabled(idle && !calibrationCaptureActive_);

    startCalibrationCaptureButton_->setEnabled(idle && !calibrationCaptureActive_);
    captureCalibrationFrameButton_->setEnabled(idle && calibrationCaptureActive_);
    calibrateCapturedFramesButton_->setEnabled(idle && calibrationCapturedFrameCount_ > 0);
    saveCalibrationResultButton_->setEnabled(idle && !calibrationCaptureActive_);
    loadCalibrationResultButton_->setEnabled(idle && !calibrationCaptureActive_);
    finishCalibrationCaptureButton_->setEnabled(idle && calibrationCaptureActive_);

    startReconstructionCaptureButton_->setEnabled(idle && !calibrationCaptureActive_);
    reconstructCapturedFramesButton_->setEnabled(idle && reconstructionCaptureReady_);
    galvoCenterButton_->setEnabled(idle && !calibrationCaptureActive_);
    rawGalvoCommandEdit_->setEnabled(idle && !calibrationCaptureActive_);
    rawGalvoCommandButton_->setEnabled(idle && !calibrationCaptureActive_);
}

} // namespace htmsr::app
