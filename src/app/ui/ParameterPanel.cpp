#include "app/ui/ParameterPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace htmsr::app {
namespace {

std::string textOf(const QLineEdit* edit)
{
    // 统一从 Qt 文本控件转换为标准字符串，便于核心层使用。
    return edit->text().toStdString();
}

cv::Rect makeRect(const QSpinBox* x, const QSpinBox* y, const QSpinBox* w, const QSpinBox* h)
{
    // ROI 参数在界面上拆成 x/y/w/h 四个输入框，这里重新组装为 OpenCV 矩形。
    return cv::Rect(x->value(), y->value(), w->value(), h->value());
}

} // namespace

ParameterPanel::ParameterPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* tabs = new QTabWidget;

    // 项目页：维护左右图像目录、标定文件路径和输出目录。
    auto* projectPage = new QWidget;
    auto* projectLayout = new QFormLayout(projectPage);
    leftCalibrationEdit_ = createPathRow(projectLayout, QString::fromUtf8("左标定目录"), true);
    rightCalibrationEdit_ = createPathRow(projectLayout, QString::fromUtf8("右标定目录"), true);
    leftReconstructionEdit_ = createPathRow(projectLayout, QString::fromUtf8("左重建目录"), true);
    rightReconstructionEdit_ = createPathRow(projectLayout, QString::fromUtf8("右重建目录"), true);
    calibrationFileEdit_ = createPathRow(projectLayout, QString::fromUtf8("标定文件"), false);
    outputDirectoryEdit_ = createPathRow(projectLayout, QString::fromUtf8("输出目录"), true);
    tabs->addTab(projectPage, QString::fromUtf8("项目"));

    // 标定页：维护棋盘格规格、方格实际尺寸和图像处理范围。
    auto* calibrationPage = new QWidget;
    auto* calibrationLayout = new QFormLayout(calibrationPage);
    boardWidthSpin_ = new QSpinBox;
    boardWidthSpin_->setRange(2, 100);
    boardWidthSpin_->setValue(9);
    boardHeightSpin_ = new QSpinBox;
    boardHeightSpin_->setRange(2, 100);
    boardHeightSpin_->setValue(6);
    squareWidthSpin_ = new QDoubleSpinBox;
    squareWidthSpin_->setRange(0.001, 10000.0);
    squareWidthSpin_->setValue(25.0);
    squareHeightSpin_ = new QDoubleSpinBox;
    squareHeightSpin_->setRange(0.001, 10000.0);
    squareHeightSpin_->setValue(25.0);
    imageBeginSpin_ = new QSpinBox;
    imageBeginSpin_->setRange(-1, 100000);
    imageBeginSpin_->setValue(-1);
    imageEndSpin_ = new QSpinBox;
    imageEndSpin_->setRange(-1, 100000);
    imageEndSpin_->setValue(-1);
    calibrationLayout->addRow(QString::fromUtf8("棋盘宽"), boardWidthSpin_);
    calibrationLayout->addRow(QString::fromUtf8("棋盘高"), boardHeightSpin_);
    calibrationLayout->addRow(QString::fromUtf8("方格宽"), squareWidthSpin_);
    calibrationLayout->addRow(QString::fromUtf8("方格高"), squareHeightSpin_);
    calibrationLayout->addRow(QString::fromUtf8("起始索引"), imageBeginSpin_);
    calibrationLayout->addRow(QString::fromUtf8("结束索引"), imageEndSpin_);
    tabs->addTab(calibrationPage, QString::fromUtf8("标定"));

    // 重建页：维护激光中心线提取、ROI 和左右匹配相关参数。
    auto* reconstructionPage = new QWidget;
    auto* reconstructionLayout = new QFormLayout(reconstructionPage);
    laserModeCombo_ = new QComboBox;
    laserModeCombo_->addItems({ QString::fromUtf8("灰度重心"), QString::fromUtf8("Steger") });
    laserModeCombo_->setCurrentIndex(0);
    laserColorCombo_ = new QComboBox;
    laserColorCombo_->addItems({ QString::fromUtf8("蓝色"), QString::fromUtf8("绿色"), QString::fromUtf8("红色"), QString::fromUtf8("灰度") });
    laserColorCombo_->setCurrentIndex(3);
    const LaserExtractionConfig defaultLaserConfig;
    leftRoiXSpin_ = createSpinRow(QString(), 0, 100000, defaultLaserConfig.leftRoi.x);
    leftRoiYSpin_ = createSpinRow(QString(), 0, 100000, defaultLaserConfig.leftRoi.y);
    leftRoiWSpin_ = createSpinRow(QString(), 1, 100000, defaultLaserConfig.leftRoi.width);
    leftRoiHSpin_ = createSpinRow(QString(), 1, 100000, defaultLaserConfig.leftRoi.height);
    rightRoiXSpin_ = createSpinRow(QString(), 0, 100000, defaultLaserConfig.rightRoi.x);
    rightRoiYSpin_ = createSpinRow(QString(), 0, 100000, defaultLaserConfig.rightRoi.y);
    rightRoiWSpin_ = createSpinRow(QString(), 1, 100000, defaultLaserConfig.rightRoi.width);
    rightRoiHSpin_ = createSpinRow(QString(), 1, 100000, defaultLaserConfig.rightRoi.height);
    grayThresholdSpin_ = createSpinRow(QString(), 0, 255, 120);
    minGraySpin_ = createSpinRow(QString(), 0, 255, 20);
    binaryThresholdSpin_ = createDoubleSpinRow(QString(), 0.0, 255.0, 100.0);
    selectionThresholdSpin_ = createDoubleSpinRow(QString(), 0.0, 255.0, 200.0);
    stripeWidthSpin_ = createDoubleSpinRow(QString(), 0.5, 100.0, 5.0);
    matchDistanceSpin_ = createDoubleSpinRow(QString(), 0.0001, 100.0, 0.5);
    removeEndpointsCheck_ = new QCheckBox(QString::fromUtf8("剔除端点"));
    removeEndpointCountSpin_ = createSpinRow(QString(), 0, 10000, 10);

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

    reconstructionLayout->addRow(QString::fromUtf8("算法"), laserModeCombo_);
    reconstructionLayout->addRow(QString::fromUtf8("颜色"), laserColorCombo_);
    reconstructionLayout->addRow(QString::fromUtf8("左ROI x/y/w/h"), leftRoiLayout);
    reconstructionLayout->addRow(QString::fromUtf8("右ROI x/y/w/h"), rightRoiLayout);
    reconstructionLayout->addRow(QString::fromUtf8("灰度阈值"), grayThresholdSpin_);
    reconstructionLayout->addRow(QString::fromUtf8("最小灰度"), minGraySpin_);
    reconstructionLayout->addRow(QString::fromUtf8("二值阈值"), binaryThresholdSpin_);
    reconstructionLayout->addRow(QString::fromUtf8("筛选阈值"), selectionThresholdSpin_);
    reconstructionLayout->addRow(QString::fromUtf8("线宽"), stripeWidthSpin_);
    reconstructionLayout->addRow(QString::fromUtf8("匹配距离"), matchDistanceSpin_);
    reconstructionLayout->addRow(removeEndpointsCheck_);
    reconstructionLayout->addRow(QString::fromUtf8("端点数量"), removeEndpointCountSpin_);
    tabs->addTab(reconstructionPage, QString::fromUtf8("重建"));

    // 采集页暂时作为在线采集预留入口，后续接入相机 SDK 时扩展。
    auto* acquisitionPage = new QWidget;
    auto* acquisitionLayout = new QFormLayout(acquisitionPage);
    acquisitionLayout->addRow(QString::fromUtf8("设备状态"), new QLineEdit(QString::fromUtf8("预留接口，未连接真实设备")));
    acquisitionLayout->addRow(QString::fromUtf8("采集源"), new QLineEdit(QString::fromUtf8("OfflineImageSequenceProvider")));
    tabs->addTab(acquisitionPage, QString::fromUtf8("采集"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(tabs);
}

void ParameterPanel::setProjectConfig(const AppProjectConfig& config)
{
    // 将持久化配置写回界面控件，保证软件重启后能恢复上次参数。
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

void ParameterPanel::setCalibrationDirectories(const std::string& leftDirectory, const std::string& rightDirectory)
{
    leftCalibrationEdit_->setText(QString::fromStdString(leftDirectory));
    rightCalibrationEdit_->setText(QString::fromStdString(rightDirectory));
}

void ParameterPanel::setReconstructionDirectories(const std::string& leftDirectory, const std::string& rightDirectory)
{
    leftReconstructionEdit_->setText(QString::fromStdString(leftDirectory));
    rightReconstructionEdit_->setText(QString::fromStdString(rightDirectory));
}

AppProjectConfig ParameterPanel::projectConfig() const
{
    // 收集当前界面上的所有项目参数，用于保存和刷新资源树。
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

CalibrationInput ParameterPanel::calibrationInput() const
{
    // 将 UI 控件值转换为核心标定服务所需的数据对象。
    CalibrationInput input;
    input.leftDirectory = textOf(leftCalibrationEdit_);
    input.rightDirectory = textOf(rightCalibrationEdit_);
    input.boardSize = cv::Size(boardWidthSpin_->value(), boardHeightSpin_->value());
    input.squareSize = cv::Size2d(squareWidthSpin_->value(), squareHeightSpin_->value());
    input.imageRange = { imageBeginSpin_->value(), imageEndSpin_->value() };
    input.outputFile = textOf(calibrationFileEdit_);
    return input;
}

ReconstructionInput ParameterPanel::reconstructionInput(const CalibrationResult& calibration) const
{
    // 将 UI 控件值转换为核心重建服务所需的数据对象。
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

QLineEdit* ParameterPanel::createPathRow(QFormLayout* form, const QString& label, bool directory)
{
    // 路径输入统一使用“文本框 + ...按钮”的形式，目录和文件选择共用该函数。
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

    auto* rowWidget = new QWidget;
    auto* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(edit);
    rowLayout->addWidget(button);

    form->addRow(label, rowWidget);
    return edit;
}

QSpinBox* ParameterPanel::createSpinRow(const QString&, int min, int max, int value)
{
    auto* spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setValue(value);
    return spin;
}

QDoubleSpinBox* ParameterPanel::createDoubleSpinRow(const QString&, double min, double max, double value)
{
    auto* spin = new QDoubleSpinBox;
    spin->setRange(min, max);
    spin->setDecimals(4);
    spin->setValue(value);
    return spin;
}

} // namespace htmsr::app
