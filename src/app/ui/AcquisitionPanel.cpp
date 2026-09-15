#include "app/ui/AcquisitionPanel.h"

#include "app/acquisition/GalvoController.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>

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
    auto* cameraGroup = new QGroupBox(QString::fromUtf8("硬件控制"));
    auto* cameraForm = new QFormLayout(cameraGroup);

    leftDeviceCombo_ = new QComboBox;
    rightDeviceCombo_ = new QComboBox;
    galvoDeviceEdit_ = new QLineEdit;
    galvoDeviceEdit_->setReadOnly(true);

    reconstructionCaptureModeCombo_ = new QComboBox;
    reconstructionCaptureModeCombo_->addItems({
        QString::fromUtf8("硬触发"),
        QString::fromUtf8("软件同步（无同步线测试）")
    });
    reconstructionCaptureModeCombo_->setCurrentIndex(1);

    triggerLineSpin_ = new QSpinBox;
    triggerLineSpin_->setRange(0, 3);
    triggerLineSpin_->setValue(0);

    frameCountEdit_ = new QLineEdit;
    frameCountEdit_->setReadOnly(true);

    outputDirectoryEdit_ = new QLineEdit("output");
    auto* outputDirectoryButton = new QPushButton(QString::fromUtf8("..."));
    outputDirectoryButton->setFixedWidth(28);

    exposureTimeSpin_ = new QDoubleSpinBox;
    exposureTimeSpin_->setRange(1.0, 10000000.0);
    exposureTimeSpin_->setDecimals(2);
    exposureTimeSpin_->setValue(3000.0);

    gainSpin_ = new QDoubleSpinBox;
    gainSpin_->setRange(0.0, 48.0);
    gainSpin_->setDecimals(2);
    gainSpin_->setValue(15.0);

    rawGalvoCommandEdit_ = new QLineEdit(QString::fromUtf8("55 AA 01 1A 1A"));
    rawGalvoCommandButton_ = new QPushButton(QString::fromUtf8("发送指令"));
    auto* rawGalvoCommandLayout = new QHBoxLayout;
    rawGalvoCommandLayout->setContentsMargins(0, 0, 0, 0);
    rawGalvoCommandLayout->addWidget(rawGalvoCommandEdit_, 1);
    rawGalvoCommandLayout->addWidget(rawGalvoCommandButton_);
    rawGalvoCommandWidget_ = new QWidget(this);
    rawGalvoCommandWidget_->setLayout(rawGalvoCommandLayout);

    galvoPortEdit_ = new QLineEdit("COM3");
    const auto ports = enumerateSerialPortNames();
    if (!ports.empty()) {
        galvoPortEdit_->setText(QString::fromStdString(ports.front()));
    }
    galvoDeviceEdit_->setText(galvoPortEdit_->text());

    cameraForm->addRow(QString::fromUtf8("左相机"), leftDeviceCombo_);
    cameraForm->addRow(QString::fromUtf8("右相机"), rightDeviceCombo_);
    cameraForm->addRow(QString::fromUtf8("振镜"), galvoDeviceEdit_);
    cameraForm->addRow(QString::fromUtf8("重建采集模式"), reconstructionCaptureModeCombo_);
    cameraForm->addRow(QString::fromUtf8("触发线"), triggerLineSpin_);
    cameraForm->addRow(QString::fromUtf8("采集帧数"), frameCountEdit_);
    cameraForm->addRow(QString::fromUtf8("保存目录"), wrapPathRow(outputDirectoryEdit_, outputDirectoryButton));
    cameraForm->addRow(QString::fromUtf8("曝光 us"), exposureTimeSpin_);
    cameraForm->addRow(QString::fromUtf8("增益"), gainSpin_);

    auto* calibrationGroup = new QGroupBox(QString::fromUtf8("相机在线标定"));
    auto* calibrationForm = new QFormLayout(calibrationGroup);
    leftCalibrationEdit_ = createPathRow(calibrationForm, QString::fromUtf8("左标定目录"), true);
    rightCalibrationEdit_ = createPathRow(calibrationForm, QString::fromUtf8("右标定目录"), true);
    calibrationFileEdit_ = createPathRow(calibrationForm, QString::fromUtf8("标定文件"), false);
    boardWidthSpin_ = createSpinRow(2, 100, 11);
    boardHeightSpin_ = createSpinRow(2, 100, 8);
    squareWidthSpin_ = createDoubleSpinRow(0.001, 10000.0, 15.0);
    squareHeightSpin_ = createDoubleSpinRow(0.001, 10000.0, 15.0);
    offlineCalibrationButton_ = new QPushButton(QString::fromUtf8("离线标定"));
    calibrationForm->addRow(QString::fromUtf8("棋盘格宽"), boardWidthSpin_);
    calibrationForm->addRow(QString::fromUtf8("棋盘格高"), boardHeightSpin_);
    calibrationForm->addRow(QString::fromUtf8("方格宽"), squareWidthSpin_);
    calibrationForm->addRow(QString::fromUtf8("方格高"), squareHeightSpin_);
    calibrationForm->addRow(offlineCalibrationButton_);

    auto* galvoGroup = new QGroupBox(QString::fromUtf8("振镜控制"));
    auto* galvoForm = new QFormLayout(galvoGroup);

    galvoTotalRotationAngleSpin_ = new QDoubleSpinBox;
    galvoTotalRotationAngleSpin_->setRange(0.0, 40.0);
    galvoTotalRotationAngleSpin_->setDecimals(4);
    galvoTotalRotationAngleSpin_->setKeyboardTracking(false);
    galvoTotalRotationAngleSpin_->setValue(20.0);

    galvoStepAngleSpin_ = new QDoubleSpinBox;
    galvoStepAngleSpin_->setRange(0.01, 650.25);
    galvoStepAngleSpin_->setDecimals(4);
    galvoStepAngleSpin_->setSingleStep(0.01);
    galvoStepAngleSpin_->setKeyboardTracking(false);
    galvoStepAngleSpin_->setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
    galvoStepAngleSpin_->setValue(0.02);

    galvoForwardSpeedSpin_ = new QSpinBox;
    galvoForwardSpeedSpin_->setRange(1, 1000);
    galvoForwardSpeedSpin_->setValue(30);

    galvoReverseSpeedSpin_ = new QSpinBox;
    galvoReverseSpeedSpin_->setRange(1, 1000);
    galvoReverseSpeedSpin_->setValue(30);

    galvoLaserDutySpin_ = new QSpinBox;
    galvoLaserDutySpin_->setRange(10, 310);
    galvoLaserDutySpin_->setValue(100);

    galvoForm->addRow(QString::fromUtf8("串口号"), galvoPortEdit_);
    galvoForm->addRow(QString::fromUtf8("总旋转角度 °"), galvoTotalRotationAngleSpin_);
    galvoForm->addRow(QString::fromUtf8("步进角度 °"), galvoStepAngleSpin_);
    galvoForm->addRow(QString::fromUtf8("正向速度 ms"), galvoForwardSpeedSpin_);
    galvoForm->addRow(QString::fromUtf8("反向速度 ms"), galvoReverseSpeedSpin_);
    galvoForm->addRow(QString::fromUtf8("激光占空比"), galvoLaserDutySpin_);

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
    workflowTabs->addTab(makeScrollArea(calibrationPage), QString::fromUtf8("标定"));
    workflowTabs->addTab(makeScrollArea(reconstructionPage), QString::fromUtf8("重建"));

    auto* root = new QVBoxLayout(this);
    root->addWidget(cameraGroup);
    root->addLayout(buttonLayout);
    root->addWidget(workflowTabs, 1);

    connect(refreshButton_, &QPushButton::clicked, this, &AcquisitionPanel::refreshDevicesRequested);
    connect(offlineCalibrationButton_, &QPushButton::clicked, this, &AcquisitionPanel::offlineCalibrationRequested);
    connect(startCalibrationCaptureButton_, &QPushButton::clicked, this, &AcquisitionPanel::startCalibrationCaptureRequested);
    connect(captureCalibrationFrameButton_, &QPushButton::clicked, this, &AcquisitionPanel::captureCalibrationFrameRequested);
    connect(calibrateCapturedFramesButton_, &QPushButton::clicked, this, &AcquisitionPanel::calibrateCapturedFramesRequested);
    connect(saveCalibrationResultButton_, &QPushButton::clicked, this, &AcquisitionPanel::saveCalibrationResultRequested);
    connect(loadCalibrationResultButton_, &QPushButton::clicked, this, &AcquisitionPanel::loadCalibrationResultRequested);
    connect(finishCalibrationCaptureButton_, &QPushButton::clicked, this, &AcquisitionPanel::finishCalibrationCaptureRequested);
    connect(startReconstructionCaptureButton_, &QPushButton::clicked, this, &AcquisitionPanel::startReconstructionCaptureRequested);
    connect(reconstructCapturedFramesButton_, &QPushButton::clicked, this, &AcquisitionPanel::reconstructCapturedFramesRequested);

    auto emitCameraConfigChanged = [this]() { emit cameraConfigChanged(); };
    connect(leftDeviceCombo_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [emitCameraConfigChanged](int) { emitCameraConfigChanged(); });
    connect(rightDeviceCombo_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [emitCameraConfigChanged](int) { emitCameraConfigChanged(); });
    connect(reconstructionCaptureModeCombo_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, emitCameraConfigChanged](int) {
        triggerLineSpin_->setEnabled(reconstructionCaptureModeCombo_->currentIndex() == 0);
        emitCameraConfigChanged();
        emit galvoConfigChanged();
    });
    connect(triggerLineSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [emitCameraConfigChanged](int) { emitCameraConfigChanged(); });
    connect(exposureTimeSpin_, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [emitCameraConfigChanged](double) { emitCameraConfigChanged(); });
    connect(gainSpin_, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [emitCameraConfigChanged](double) { emitCameraConfigChanged(); });

    auto sendRawGalvoCommand = [this]() {
        rememberRawGalvoCommand();
        emit sendRawGalvoCommandRequested(rawGalvoCommandEdit_->text());
    };
    connect(rawGalvoCommandButton_, &QPushButton::clicked, this, sendRawGalvoCommand);
    connect(rawGalvoCommandEdit_, &QLineEdit::returnPressed, this, sendRawGalvoCommand);
    connect(rawGalvoCommandEdit_, &QLineEdit::textEdited, this, [this](const QString&) {
        resetRawGalvoCommandHistoryNavigation();
    });
    rawGalvoCommandEdit_->installEventFilter(this);
    connect(galvoPortEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        galvoDeviceEdit_->setText(text);
        updateDerivedFrameCount();
        emit galvoConfigChanged();
    });
    connect(galvoTotalRotationAngleSpin_, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        updateDerivedFrameCount();
        emit galvoConfigChanged();
        emit galvoMotionParametersChanged();
    });
    connect(galvoStepAngleSpin_, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        updateDerivedFrameCount();
        emit galvoConfigChanged();
        emit galvoMotionParametersChanged();
    });
    connect(galvoForwardSpeedSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoReverseSpeedSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(galvoLaserDutySpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    connect(outputDirectoryButton, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(this, QString::fromUtf8("选择采集输出目录"), outputDirectoryEdit_->text());
        if (!directory.isEmpty()) {
            outputDirectoryEdit_->setText(directory);
        }
    });
    triggerLineSpin_->setEnabled(reconstructionCaptureModeCombo_->currentIndex() == 0);
    updateDerivedFrameCount();
    updateActionButtons();
}

void AcquisitionPanel::updateDerivedFrameCount()
{
    // 自动旋转角度在协议中是整数，预览值应与实际下发的角度一致。
    const int deviceAngleEstimate = static_cast<int>(std::lround(galvoTotalRotationAngleSpin_->value()));
    derivedFrameCount_ = frameCountForGalvoScan(deviceAngleEstimate, galvoStepAngleSpin_->value());
    frameCountEdit_->setText(QString::number(derivedFrameCount_));
    frameCountEdit_->setToolTip(QString::fromUtf8("按当前设置预估：总角 %1° ÷ 步进 %2°，重建采集前以设备回读结果为准。")
        .arg(deviceAngleEstimate)
        .arg(galvoStepAngleSpin_->value(), 0, 'f', 4));
}

void AcquisitionPanel::setFrameCountFromDevice(double stepAngleDeg, int totalRotationAngleDeg)
{
    derivedFrameCount_ = frameCountForGalvoScan(totalRotationAngleDeg, stepAngleDeg);
    frameCountEdit_->setText(QString::number(derivedFrameCount_));
    frameCountEdit_->setToolTip(QString::fromUtf8("已按设备回读计算：总角 %1° ÷ 步进 %2°。")
        .arg(totalRotationAngleDeg)
        .arg(stepAngleDeg, 0, 'f', 4));
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
    const QSignalBlocker leftBlocker(leftDeviceCombo_);
    const QSignalBlocker rightBlocker(rightDeviceCombo_);

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
        ? QString::fromUtf8("未枚举到真实相机，请检查相机连接或驱动")
        : QString::fromUtf8("已枚举到 %1 台相机").arg(static_cast<qulonglong>(devices.size())));
    emit cameraConfigChanged();
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
    config.useMockProvider = false;
    config.frameCount = derivedFrameCount_;
    config.outputDirectory = outputDirectoryEdit_->text().toStdString();
    config.leftParameters.exposureTime = exposureTimeSpin_->value();
    config.rightParameters.exposureTime = exposureTimeSpin_->value();
    config.leftParameters.gain = gainSpin_->value();
    config.rightParameters.gain = gainSpin_->value();
    config.leftParameters.useHardwareTrigger = false;
    config.rightParameters.useHardwareTrigger = false;
    config.leftParameters.triggerSourceLine = triggerLineSpin_->value();
    config.rightParameters.triggerSourceLine = triggerLineSpin_->value();
    config.leftParameters.grabTimeoutMs = 1000;
    config.rightParameters.grabTimeoutMs = 1000;
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
    config.galvo.baudRate = 115200;
    config.galvo.commandTimeoutMs = 500;
    config.galvo.syncMode = reconstructionCaptureModeCombo_->currentIndex() == 0
        ? GalvoSyncMode::Sync
        : GalvoSyncMode::Async;
    config.galvo.direction = GalvoScanDirection::Forward;
    config.galvo.captureIntervalMs = 30;
    config.galvo.continuousCaptureWaitMs = 30;
    config.galvo.stepAngleDeg = galvoStepAngleSpin_->value();
    config.galvo.autoRotationAngleDeg = static_cast<int>(std::lround(config.totalRotationAngleDeg));
    config.galvo.forwardSpeedMs = galvoForwardSpeedSpin_->value();
    config.galvo.reverseSpeedMs = galvoReverseSpeedSpin_->value();
    config.galvo.laserDuty = galvoLaserDutySpin_->value();
    config.galvo.voltageRangeV = 7.0;
    config.forceRecalibration = false;
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
    input.imageRange = { -1, -1 };
    input.outputFile = textOf(calibrationFileEdit_);
    return input;
}

ReconstructionInput AcquisitionPanel::reconstructionInput(const CalibrationResult& calibration) const
{
    ReconstructionInput input;
    input.leftDirectory = textOf(leftReconstructionEdit_);
    input.rightDirectory = textOf(rightReconstructionEdit_);
    input.imageRange = { -1, -1 };
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

void AcquisitionPanel::setStatusText(const QString& text)
{
    Q_UNUSED(text);
}

void AcquisitionPanel::setResultSummary(const QString& text)
{
    Q_UNUSED(text);
}

QWidget* AcquisitionPanel::rawGalvoCommandWidget() const
{
    return rawGalvoCommandWidget_;
}

bool AcquisitionPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == rawGalvoCommandEdit_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            navigateRawGalvoCommandHistory(-1);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down) {
            navigateRawGalvoCommandHistory(1);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void AcquisitionPanel::rememberRawGalvoCommand()
{
    const QString command = rawGalvoCommandEdit_->text();
    if (!command.trimmed().isEmpty()) {
        rawGalvoCommandHistory_.append(command);
    }
    resetRawGalvoCommandHistoryNavigation();
}

void AcquisitionPanel::navigateRawGalvoCommandHistory(int direction)
{
    if (rawGalvoCommandHistory_.isEmpty()) {
        return;
    }

    if (!rawGalvoCommandHistoryBrowsing_) {
        if (direction > 0) {
            return;
        }
        rawGalvoCommandDraft_ = rawGalvoCommandEdit_->text();
        rawGalvoCommandHistoryIndex_ = rawGalvoCommandHistory_.size();
        rawGalvoCommandHistoryBrowsing_ = true;
    }

    if (direction < 0) {
        if (rawGalvoCommandHistoryIndex_ > 0) {
            --rawGalvoCommandHistoryIndex_;
        }
        rawGalvoCommandEdit_->setText(rawGalvoCommandHistory_.at(rawGalvoCommandHistoryIndex_));
    } else if (rawGalvoCommandHistoryIndex_ < rawGalvoCommandHistory_.size() - 1) {
        ++rawGalvoCommandHistoryIndex_;
        rawGalvoCommandEdit_->setText(rawGalvoCommandHistory_.at(rawGalvoCommandHistoryIndex_));
    } else {
        const QString draft = rawGalvoCommandDraft_;
        resetRawGalvoCommandHistoryNavigation();
        rawGalvoCommandEdit_->setText(draft);
    }

    rawGalvoCommandEdit_->setCursorPosition(rawGalvoCommandEdit_->text().size());
}

void AcquisitionPanel::resetRawGalvoCommandHistoryNavigation()
{
    rawGalvoCommandHistoryBrowsing_ = false;
    rawGalvoCommandHistoryIndex_ = rawGalvoCommandHistory_.size();
    rawGalvoCommandDraft_.clear();
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

    offlineCalibrationButton_->setEnabled(idle && !calibrationCaptureActive_);
    startCalibrationCaptureButton_->setEnabled(idle && !calibrationCaptureActive_);
    captureCalibrationFrameButton_->setEnabled(idle && calibrationCaptureActive_);
    calibrateCapturedFramesButton_->setEnabled(idle && calibrationCapturedFrameCount_ > 0);
    saveCalibrationResultButton_->setEnabled(idle && !calibrationCaptureActive_);
    loadCalibrationResultButton_->setEnabled(idle && !calibrationCaptureActive_);
    finishCalibrationCaptureButton_->setEnabled(idle && calibrationCaptureActive_);

    startReconstructionCaptureButton_->setEnabled(idle && !calibrationCaptureActive_);
    reconstructCapturedFramesButton_->setEnabled(idle && reconstructionCaptureReady_);
    galvoTotalRotationAngleSpin_->setEnabled(idle);
    galvoStepAngleSpin_->setEnabled(idle);
    rawGalvoCommandEdit_->setEnabled(idle && !calibrationCaptureActive_);
    rawGalvoCommandButton_->setEnabled(idle && !calibrationCaptureActive_);
}

} // namespace htmsr::app
