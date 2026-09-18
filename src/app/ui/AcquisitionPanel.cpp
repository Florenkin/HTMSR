#include "app/ui/AcquisitionPanel.h"

#include "app/acquisition/GalvoController.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
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
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>
#include <algorithm>

namespace htmsr::app {
namespace {

constexpr char kDefaultOutputDirectory[] = "C:/PROJECT/HTMSR/output";

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

std::string pathOf(const QLineEdit* edit)
{
    const QString directory = edit->text().trimmed();
    return directory.isEmpty() ? std::string{} : QDir::cleanPath(QDir::fromNativeSeparators(directory)).toStdString();
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
    函数功能：构造在线采集参数面板，初始化设备、曝光、触发和振镜参数等控件
    输入：
        parent：Qt 父控件
    输出：
        无（构造后完成采集面板界面布局和信号连接）
*/
AcquisitionPanel::AcquisitionPanel(QWidget* parent)
    : QWidget(parent)
{
    const AcquisitionParameterConfig defaults;
    auto* cameraGroup = new QGroupBox(QString::fromUtf8("硬件控制"));
    auto* cameraForm = new QFormLayout(cameraGroup);

    leftDeviceCombo_ = new QComboBox;
    rightDeviceCombo_ = new QComboBox;
    galvoDeviceEdit_ = new QLineEdit;
    galvoDeviceEdit_->setObjectName("galvoSerialPort");
    galvoDeviceEdit_->setReadOnly(true);

    reconstructionCaptureModeCombo_ = new QComboBox;
    reconstructionCaptureModeCombo_->addItems({
        QString::fromUtf8("硬触发"),
        QString::fromUtf8("软件抓图（自由取流）")
    });
    reconstructionCaptureModeCombo_->setCurrentIndex(defaults.useHardwareTrigger ? 0 : 1);
    reconstructionCaptureModeCombo_->setToolTip(QString::fromUtf8(
        "重建默认使用硬触发：同一振镜同步脉冲接到左右相机的 Line5，每次步进各拍一张。实时预览仍自由取流。"));

    triggerLineSpin_ = new QSpinBox;
    triggerLineSpin_->setRange(0, 5);
    triggerLineSpin_->setValue(defaults.triggerSourceLine);
    triggerLineSpin_->setToolTip(QString::fromUtf8("相机 TriggerSource，默认 Line5；所选线路必须由实际相机支持并接入同一触发信号。"));

    frameCountEdit_ = new QLineEdit;
    frameCountEdit_->setReadOnly(true);

    outputDirectory_ = kDefaultOutputDirectory;

    exposureTimeSpin_ = new QDoubleSpinBox;
    exposureTimeSpin_->setRange(1.0, 10000000.0);
    exposureTimeSpin_->setDecimals(2);
    exposureTimeSpin_->setValue(defaults.exposureTime);

    rawGalvoCommandEdit_ = new QLineEdit(QString::fromUtf8("55 AA 01 1A 1A"));
    rawGalvoCommandButton_ = new QPushButton(QString::fromUtf8("发送指令"));
    auto* rawGalvoCommandLayout = new QHBoxLayout;
    rawGalvoCommandLayout->setContentsMargins(0, 0, 0, 0);
    rawGalvoCommandLayout->addWidget(rawGalvoCommandEdit_, 1);
    rawGalvoCommandLayout->addWidget(rawGalvoCommandButton_);
    rawGalvoCommandWidget_ = new QWidget(this);
    rawGalvoCommandWidget_->setLayout(rawGalvoCommandLayout);

    cameraForm->addRow(QString::fromUtf8("左相机"), leftDeviceCombo_);
    cameraForm->addRow(QString::fromUtf8("右相机"), rightDeviceCombo_);
    cameraForm->addRow(QString::fromUtf8("振镜串口"), galvoDeviceEdit_);
    cameraForm->addRow(QString::fromUtf8("触发线"), triggerLineSpin_);
    cameraForm->addRow(QString::fromUtf8("曝光 us"), exposureTimeSpin_);
    cameraForm->addRow(QString::fromUtf8("采集模式"), reconstructionCaptureModeCombo_);

    auto* calibrationGroup = new QGroupBox(QString::fromUtf8("相机标定"));
    auto* calibrationForm = new QFormLayout(calibrationGroup);
    leftCalibrationEdit_ = createPathRow(calibrationForm, QString::fromUtf8("左标定目录"), true);
    rightCalibrationEdit_ = createPathRow(calibrationForm, QString::fromUtf8("右标定目录"), true);
    calibrationFileEdit_ = createPathRow(calibrationForm, QString::fromUtf8("标定文件"), false);
    leftCalibrationEdit_->setObjectName("leftCalibrationDirectory");
    rightCalibrationEdit_->setObjectName("rightCalibrationDirectory");
    calibrationFileEdit_->setObjectName("calibrationFile");
    boardWidthSpin_ = createSpinRow(2, 100, 11);
    boardHeightSpin_ = createSpinRow(2, 100, 8);
    squareWidthSpin_ = createDoubleSpinRow(0.001, 10000.0, 15.0);
    squareHeightSpin_ = createDoubleSpinRow(0.001, 10000.0, 15.0);
    calibrationForm->addRow(QString::fromUtf8("棋盘格宽"), boardWidthSpin_);
    calibrationForm->addRow(QString::fromUtf8("棋盘格高"), boardHeightSpin_);
    calibrationForm->addRow(QString::fromUtf8("方格宽"), squareWidthSpin_);
    calibrationForm->addRow(QString::fromUtf8("方格高"), squareHeightSpin_);

    auto* galvoGroup = new QGroupBox(QString::fromUtf8("振镜控制"));
    auto* galvoForm = new QFormLayout(galvoGroup);

    galvoTotalRotationAngleSpin_ = new QDoubleSpinBox;
    galvoTotalRotationAngleSpin_->setObjectName("galvoTotalRotationAngle");
    galvoTotalRotationAngleSpin_->setRange(0.0, 40.0);
    galvoTotalRotationAngleSpin_->setDecimals(4);
    galvoTotalRotationAngleSpin_->setKeyboardTracking(false);
    galvoTotalRotationAngleSpin_->setValue(defaults.totalRotationAngleDeg);

    galvoStepAngleSpin_ = new QDoubleSpinBox;
    galvoStepAngleSpin_->setObjectName("galvoStepAngle");
    galvoStepAngleSpin_->setRange(0.01, 650.25);
    galvoStepAngleSpin_->setDecimals(4);
    galvoStepAngleSpin_->setSingleStep(0.01);
    galvoStepAngleSpin_->setKeyboardTracking(false);
    galvoStepAngleSpin_->setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
    galvoStepAngleSpin_->setValue(defaults.stepAngleDeg);

    galvoSpeedSpin_ = new QSpinBox;
    galvoSpeedSpin_->setObjectName("galvoSpeed");
    galvoSpeedSpin_->setRange(1, 1000);
    galvoSpeedSpin_->setValue(defaults.speedMs);
    galvoSpeedSpin_->setToolTip(QString::fromUtf8("同时设置正向和反向转动速度。"));

    galvoForm->addRow(QString::fromUtf8("采集帧数"), frameCountEdit_);
    galvoForm->addRow(QString::fromUtf8("总旋转角度 °"), galvoTotalRotationAngleSpin_);
    galvoForm->addRow(QString::fromUtf8("步进角度 °"), galvoStepAngleSpin_);
    galvoForm->addRow(QString::fromUtf8("正反向速度 ms"), galvoSpeedSpin_);

    auto* reconstructionGroup = new QGroupBox(QString::fromUtf8("点云重建"));
    auto* reconstructionForm = new QFormLayout(reconstructionGroup);
    leftReconstructionEdit_ = createPathRow(reconstructionForm, QString::fromUtf8("左重建目录"), true);
    rightReconstructionEdit_ = createPathRow(reconstructionForm, QString::fromUtf8("右重建目录"), true);
    leftReconstructionEdit_->setObjectName("leftReconstructionDirectory");
    rightReconstructionEdit_->setObjectName("rightReconstructionDirectory");
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
    captureCalibrationFrameButton_ = new QPushButton(QString::fromUtf8("采集"));
    captureCalibrationFrameButton_->setObjectName("captureCalibrationFrame");
    calibrateCapturedFramesButton_ = new QPushButton(QString::fromUtf8("标定"));
    calibrateCapturedFramesButton_->setObjectName("calibrateFrames");
    captureCalibrationFrameButton_->setToolTip(QString::fromUtf8("首次点击自动连接相机，采集并保存一组左右标定图像。"));
    calibrateCapturedFramesButton_->setToolTip(QString::fromUtf8("标定左右目录中的图像，并自动保存标定结果。"));
    exportCalibrationButton_ = new QPushButton(QString::fromUtf8("导出"));
    exportCalibrationButton_->setObjectName("exportCalibration");
    exportCalibrationButton_->setToolTip(QString::fromUtf8("将当前标定结果另存到指定路径。"));
    startReconstructionCaptureButton_ = new QPushButton(QString::fromUtf8("采集"));
    startReconstructionCaptureButton_->setObjectName("startReconstructionCapture");
    reconstructCapturedFramesButton_ = new QPushButton(QString::fromUtf8("重建"));
    reconstructCapturedFramesButton_->setObjectName("reconstructFrames");
    reconstructCapturedFramesButton_->setToolTip(QString::fromUtf8("重建所选目录中的左右图像，并自动保存点云。"));
    exportReconstructionButton_ = new QPushButton(QString::fromUtf8("导出"));
    exportReconstructionButton_->setObjectName("exportReconstruction");
    exportReconstructionButton_->setToolTip(QString::fromUtf8("将当前点云另存为 PCD 或 TXT。"));

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(refreshButton_);

    auto* calibrationButtonLayout = new QHBoxLayout;
    calibrationButtonLayout->addWidget(captureCalibrationFrameButton_);
    calibrationButtonLayout->addWidget(calibrateCapturedFramesButton_);
    calibrationButtonLayout->addWidget(exportCalibrationButton_);

    auto* reconstructionButtonLayout = new QHBoxLayout;
    reconstructionButtonLayout->addWidget(startReconstructionCaptureButton_);
    reconstructionButtonLayout->addWidget(reconstructCapturedFramesButton_);
    reconstructionButtonLayout->addWidget(exportReconstructionButton_);

    auto* calibrationPage = new QWidget;
    auto* calibrationPageLayout = new QVBoxLayout(calibrationPage);
    calibrationPageLayout->addWidget(calibrationGroup);
    calibrationPageLayout->addLayout(calibrationButtonLayout);
    calibrationPageLayout->addStretch();

    auto* reconstructionPage = new QWidget;
    auto* reconstructionPageLayout = new QVBoxLayout(reconstructionPage);
    reconstructionPageLayout->addWidget(galvoGroup);
    reconstructionPageLayout->addWidget(reconstructionGroup);
    reconstructionPageLayout->addLayout(reconstructionButtonLayout);
    reconstructionPageLayout->addStretch();

    auto* workflowTabs = new QTabWidget;
    workflowTabs_ = workflowTabs;
    workflowTabs->addTab(makeScrollArea(calibrationPage), QString::fromUtf8("标定"));
    workflowTabs->addTab(makeScrollArea(reconstructionPage), QString::fromUtf8("重建"));

    auto* root = new QVBoxLayout(this);
    root->addWidget(cameraGroup);
    root->addLayout(buttonLayout);
    root->addWidget(workflowTabs, 1);

    connect(refreshButton_, &QPushButton::clicked, this, &AcquisitionPanel::refreshDevicesRequested);
    connect(captureCalibrationFrameButton_, &QPushButton::clicked, this, &AcquisitionPanel::captureCalibrationFrameRequested);
    connect(calibrateCapturedFramesButton_, &QPushButton::clicked, this, &AcquisitionPanel::calibrateCapturedFramesRequested);
    connect(exportCalibrationButton_, &QPushButton::clicked, this, &AcquisitionPanel::exportCalibrationRequested);
    connect(workflowTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index != 0 && calibrationCaptureActive_) {
            emit finishCalibrationCaptureRequested();
        }
    });
    connect(leftCalibrationEdit_, &QLineEdit::textChanged, this, [this]() { updateActionButtons(); });
    connect(rightCalibrationEdit_, &QLineEdit::textChanged, this, [this]() { updateActionButtons(); });
    connect(leftReconstructionEdit_, &QLineEdit::textChanged, this, [this]() { updateActionButtons(); });
    connect(rightReconstructionEdit_, &QLineEdit::textChanged, this, [this]() { updateActionButtons(); });
    connect(calibrationFileEdit_, &QLineEdit::editingFinished, this, [this]() {
        if (!calibrationFileEdit_->isModified()) {
            return;
        }
        calibrationFileEdit_->setModified(false);
        emit calibrationFileSelected(calibrationFileEdit_->text().trimmed());
    });
    connect(startReconstructionCaptureButton_, &QPushButton::clicked, this, &AcquisitionPanel::startReconstructionCaptureRequested);
    connect(reconstructCapturedFramesButton_, &QPushButton::clicked, this, &AcquisitionPanel::reconstructCapturedFramesRequested);
    connect(exportReconstructionButton_, &QPushButton::clicked, this, &AcquisitionPanel::exportReconstructionRequested);

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
    connect(galvoSpeedSpin_, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { emit galvoConfigChanged(); });
    // 所有可编辑的算法和采集参数都通知自动保存；恢复配置时由面板阻断信号。
    for (auto* spin : findChildren<QSpinBox*>())
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { emit projectConfigChanged(); });
    for (auto* spin : findChildren<QDoubleSpinBox*>())
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { emit projectConfigChanged(); });
    for (auto* combo : {laserModeCombo_, laserColorCombo_, reconstructionCaptureModeCombo_})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { emit projectConfigChanged(); });
    connect(removeEndpointsCheck_, &QCheckBox::toggled, this, [this](bool) { emit projectConfigChanged(); });
    for (auto* edit : {leftCalibrationEdit_, rightCalibrationEdit_, leftReconstructionEdit_, rightReconstructionEdit_, calibrationFileEdit_})
        connect(edit, &QLineEdit::textChanged, this, [this]() { emit projectConfigChanged(); });
    triggerLineSpin_->setEnabled(reconstructionCaptureModeCombo_->currentIndex() == 0);
    updateDerivedFrameCount();
    setSerialPorts(enumerateSerialPortNames());
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

// 自动识别在线串口；多个端口时保留仍在线的已识别端口，避免误用其他设备。
void AcquisitionPanel::setSerialPorts(const std::vector<std::string>& ports)
{
    const std::string previous = galvoPortName_;
    if (std::find(ports.begin(), ports.end(), galvoPortName_) == ports.end()) {
        galvoPortName_ = ports.size() == 1 ? ports.front() : std::string{};
    }
    galvoDeviceEdit_->setText(QString::fromStdString(galvoPortName_));
    const QString hint = ports.empty() ? QString{}
        : QString::fromUtf8("检测到多个串口，暂时无法自动确定振镜端口");
    galvoDeviceEdit_->setPlaceholderText(hint);
    galvoDeviceEdit_->setToolTip(galvoPortName_.empty() ? hint
        : QString::fromUtf8("软件已自动识别此在线串口，采集前会验证振镜参数通信。"));
    if (previous != galvoPortName_) {
        updateDerivedFrameCount();
        emit galvoConfigChanged();
    }
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
    config.outputDirectory = outputDirectory_;
    config.leftParameters.exposureTime = exposureTimeSpin_->value();
    config.rightParameters.exposureTime = exposureTimeSpin_->value();
    config.leftParameters.gain = baseConfig_.acquisitionParameters.cameraGain;
    config.rightParameters.gain = baseConfig_.acquisitionParameters.cameraGain;
    config.leftParameters.useHardwareTrigger = false;
    config.rightParameters.useHardwareTrigger = false;
    config.leftParameters.triggerSourceLine = triggerLineSpin_->value();
    config.rightParameters.triggerSourceLine = triggerLineSpin_->value();
    config.leftParameters.grabTimeoutMs = baseConfig_.acquisitionParameters.grabTimeoutMs;
    config.rightParameters.grabTimeoutMs = baseConfig_.acquisitionParameters.grabTimeoutMs;
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
    const bool useHardwareTrigger = reconstructionCaptureModeCombo_->currentIndex() == 0;
    config.stereoCamera.leftParameters.useHardwareTrigger = useHardwareTrigger;
    config.stereoCamera.rightParameters.useHardwareTrigger = useHardwareTrigger;
    config.totalRotationAngleDeg = galvoTotalRotationAngleSpin_->value();
    config.galvo.portName = galvoPortName_;
    const auto& stored = baseConfig_.acquisitionParameters;
    config.galvo.baudRate = stored.baudRate;
    config.galvo.commandTimeoutMs = stored.commandTimeoutMs;
    // 控制器同步命令不决定海康相机是否启用硬触发。
    config.galvo.syncMode = stored.syncMode ? GalvoSyncMode::Sync : GalvoSyncMode::Async;
    config.galvo.direction = stored.reverseDirection ? GalvoScanDirection::Reverse : GalvoScanDirection::Forward;
    config.galvo.captureIntervalMs = stored.captureIntervalMs;
    config.galvo.continuousCaptureWaitMs = stored.continuousCaptureWaitMs;
    config.galvo.stepAngleDeg = galvoStepAngleSpin_->value();
    config.galvo.autoRotationAngleDeg = static_cast<int>(std::lround(config.totalRotationAngleDeg));
    config.galvo.forwardSpeedMs = galvoSpeedSpin_->value();
    config.galvo.reverseSpeedMs = galvoSpeedSpin_->value();
    config.galvo.laserDuty = stored.laserDuty;
    config.galvo.voltageRangeV = stored.voltageRangeV;
    config.forceRecalibration = false;
    return config;
}

AppProjectConfig AcquisitionPanel::projectConfig() const
{
    AppProjectConfig config = baseConfig_;
    config.leftCalibrationDirectory = textOf(leftCalibrationEdit_);
    config.rightCalibrationDirectory = textOf(rightCalibrationEdit_);
    const auto reconstruction = reconstructionInput({});
    config.leftReconstructionDirectory = reconstruction.leftDirectory;
    config.rightReconstructionDirectory = reconstruction.rightDirectory;
    config.calibrationFile = textOf(calibrationFileEdit_);
    config.outputDirectory = outputDirectory_;
    config.calibrationInput = calibrationInput();
    config.laserConfig = reconstruction.laserConfig;
    config.matchDistanceThreshold = matchDistanceSpin_->value();
    auto& acquisition = config.acquisitionParameters;
    acquisition.exposureTime = exposureTimeSpin_->value();
    acquisition.useHardwareTrigger = reconstructionCaptureModeCombo_->currentIndex() == 0;
    acquisition.triggerSourceLine = triggerLineSpin_->value();
    acquisition.stepAngleDeg = galvoStepAngleSpin_->value();
    acquisition.totalRotationAngleDeg = galvoTotalRotationAngleSpin_->value();
    acquisition.speedMs = galvoSpeedSpin_->value();
    return config;
}

CalibrationInput AcquisitionPanel::calibrationInput() const
{
    CalibrationInput input;
    input.leftDirectory = textOf(leftCalibrationEdit_);
    input.rightDirectory = textOf(rightCalibrationEdit_);
    input.boardSize = cv::Size(boardWidthSpin_->value(), boardHeightSpin_->value());
    input.squareSize = cv::Size2d(squareWidthSpin_->value(), squareHeightSpin_->value());
    input.imageRange = baseConfig_.calibrationInput.imageRange;
    input.outputFile = textOf(calibrationFileEdit_);
    return input;
}

ReconstructionInput AcquisitionPanel::reconstructionInput(const CalibrationResult& calibration) const
{
    ReconstructionInput input;
    input.leftDirectory = pathOf(leftReconstructionEdit_);
    input.rightDirectory = pathOf(rightReconstructionEdit_);
    input.imageRange = baseConfig_.reconstructionImageRange;
    input.calibration = calibration;
    input.laserConfig.filterStegerPoints = baseConfig_.laserConfig.filterStegerPoints;
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
    // 恢复界面参数时不下发振镜命令或重启相机预览。
    const QSignalBlocker blocker(this);
    baseConfig_ = config;
    const auto& acquisition = config.acquisitionParameters;
    exposureTimeSpin_->setValue(acquisition.exposureTime);
    reconstructionCaptureModeCombo_->setCurrentIndex(acquisition.useHardwareTrigger ? 0 : 1);
    triggerLineSpin_->setValue(acquisition.triggerSourceLine);
    galvoTotalRotationAngleSpin_->setValue(acquisition.totalRotationAngleDeg);
    galvoStepAngleSpin_->setValue(acquisition.stepAngleDeg);
    galvoSpeedSpin_->setValue(acquisition.speedMs);
    leftCalibrationEdit_->setText(QString::fromStdString(config.restoreInputPaths ? config.leftCalibrationDirectory : std::string{}));
    rightCalibrationEdit_->setText(QString::fromStdString(config.restoreInputPaths ? config.rightCalibrationDirectory : std::string{}));
    setReconstructionDirectories(config.restoreInputPaths ? config.leftReconstructionDirectory : std::string{},
        config.restoreInputPaths ? config.rightReconstructionDirectory : std::string{});
    calibrationFileEdit_->setText(QString::fromStdString(config.restoreInputPaths ? config.calibrationFile : std::string{}));
    outputDirectory_ = config.outputDirectory.empty() ? kDefaultOutputDirectory : config.outputDirectory;
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
    updateDerivedFrameCount();
}

void AcquisitionPanel::commitPendingEdits()
{
    for (auto* spin : findChildren<QAbstractSpinBox*>()) {
        spin->interpretText();
    }
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
    leftReconstructionEdit_->setText(QDir::fromNativeSeparators(QString::fromStdString(leftDirectory)));
    rightReconstructionEdit_->setText(QDir::fromNativeSeparators(QString::fromStdString(rightDirectory)));
}

QLineEdit* AcquisitionPanel::createPathRow(QFormLayout* form, const QString& label, bool directory)
{
    auto* edit = new QLineEdit;
    auto* button = new QPushButton(QString::fromUtf8("..."));
    button->setFixedWidth(28);
    connect(button, &QPushButton::clicked, this, [edit, directory, this]() {
        const QString value = directory
            ? QFileDialog::getExistingDirectory(this, QString::fromUtf8("选择目录"), edit->text())
            : QFileDialog::getOpenFileName(this, QString::fromUtf8("选择标定文件"), edit->text(), QString::fromUtf8("YAML (*.yml *.yaml);;所有文件 (*.*)"));
        if (!value.isEmpty()) {
            edit->setText(value);
            if (!directory) {
                emit calibrationFileSelected(value);
            }
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

void AcquisitionPanel::setResultAvailability(bool calibrationAvailable, bool reconstructionAvailable)
{
    calibrationResultAvailable_ = calibrationAvailable;
    reconstructionResultAvailable_ = reconstructionAvailable;
    updateActionButtons();
}

void AcquisitionPanel::updateActionButtons()
{
    const bool idle = !busy_;
    workflowTabs_->tabBar()->setEnabled(idle);
    refreshButton_->setEnabled(idle && !calibrationCaptureActive_);

    captureCalibrationFrameButton_->setEnabled(idle);
    const bool hasCalibrationDirectories = !leftCalibrationEdit_->text().trimmed().isEmpty() &&
        !rightCalibrationEdit_->text().trimmed().isEmpty();
    calibrateCapturedFramesButton_->setEnabled(idle && (hasCalibrationDirectories || calibrationCapturedFrameCount_ > 0));
    exportCalibrationButton_->setEnabled(idle && calibrationResultAvailable_);
    leftCalibrationEdit_->parentWidget()->setEnabled(idle);
    rightCalibrationEdit_->parentWidget()->setEnabled(idle);
    calibrationFileEdit_->parentWidget()->setEnabled(idle);
    boardWidthSpin_->setEnabled(idle);
    boardHeightSpin_->setEnabled(idle);
    squareWidthSpin_->setEnabled(idle);
    squareHeightSpin_->setEnabled(idle);

    startReconstructionCaptureButton_->setEnabled(idle && !calibrationCaptureActive_);
    const bool hasReconstructionDirectories = !pathOf(leftReconstructionEdit_).empty() && !pathOf(rightReconstructionEdit_).empty();
    reconstructCapturedFramesButton_->setEnabled(idle && hasReconstructionDirectories);
    exportReconstructionButton_->setEnabled(idle && reconstructionResultAvailable_);
    leftReconstructionEdit_->parentWidget()->setEnabled(idle);
    rightReconstructionEdit_->parentWidget()->setEnabled(idle);
    galvoTotalRotationAngleSpin_->setEnabled(idle);
    galvoStepAngleSpin_->setEnabled(idle);
    galvoSpeedSpin_->setEnabled(idle);
    rawGalvoCommandEdit_->setEnabled(idle && !calibrationCaptureActive_);
    rawGalvoCommandButton_->setEnabled(idle && !calibrationCaptureActive_);
}

} // namespace htmsr::app
