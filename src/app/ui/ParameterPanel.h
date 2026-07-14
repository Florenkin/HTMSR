#pragma once

#include "core/Types.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QSpinBox;

namespace htmsr::app {

class ParameterPanel final : public QWidget {
    Q_OBJECT

public:
    explicit ParameterPanel(QWidget* parent = nullptr);

    /*
        函数功能：将项目配置恢复到参数面板控件
        输入：
            config：项目配置数据
        输出：
            无
    */
    void setProjectConfig(const AppProjectConfig& config);

    /*
        函数功能：将采集会话输出的左右目录写入重建输入目录
        输入：
            leftDirectory：左图像保存目录
            rightDirectory：右图像保存目录
        输出：
            无（函数会更新重建目录输入框）
    */
    void setReconstructionDirectories(const std::string& leftDirectory, const std::string& rightDirectory);

    /*
        函数功能：从参数面板读取完整项目配置
        输入：
            无
        输出：
            返回值：当前 UI 中的项目路径、标定参数和重建参数
    */
    AppProjectConfig projectConfig() const;

    /*
        函数功能：从参数面板读取双目标定输入参数
        输入：
            无
        输出：
            返回值：标定目录、棋盘格参数、图像范围和输出文件路径
    */
    CalibrationInput calibrationInput() const;

    /*
        函数功能：从参数面板读取离线重建输入参数
        输入：
            calibration：当前可用的双目标定结果
        输出：
            返回值：重建目录、标定结果、线提取参数和匹配阈值
    */
    ReconstructionInput reconstructionInput(const CalibrationResult& calibration) const;

private:
    QLineEdit* createPathRow(QFormLayout* form, const QString& label, bool directory);
    QSpinBox* createSpinRow(const QString& label, int min, int max, int value);
    QDoubleSpinBox* createDoubleSpinRow(const QString& label, double min, double max, double value);

    QLineEdit* leftCalibrationEdit_ = nullptr;
    QLineEdit* rightCalibrationEdit_ = nullptr;
    QLineEdit* leftReconstructionEdit_ = nullptr;
    QLineEdit* rightReconstructionEdit_ = nullptr;
    QLineEdit* calibrationFileEdit_ = nullptr;
    QLineEdit* outputDirectoryEdit_ = nullptr;

    QSpinBox* boardWidthSpin_ = nullptr;
    QSpinBox* boardHeightSpin_ = nullptr;
    QDoubleSpinBox* squareWidthSpin_ = nullptr;
    QDoubleSpinBox* squareHeightSpin_ = nullptr;
    QSpinBox* imageBeginSpin_ = nullptr;
    QSpinBox* imageEndSpin_ = nullptr;

    QComboBox* laserModeCombo_ = nullptr;
    QComboBox* laserColorCombo_ = nullptr;
    QSpinBox* leftRoiXSpin_ = nullptr;
    QSpinBox* leftRoiYSpin_ = nullptr;
    QSpinBox* leftRoiWSpin_ = nullptr;
    QSpinBox* leftRoiHSpin_ = nullptr;
    QSpinBox* rightRoiXSpin_ = nullptr;
    QSpinBox* rightRoiYSpin_ = nullptr;
    QSpinBox* rightRoiWSpin_ = nullptr;
    QSpinBox* rightRoiHSpin_ = nullptr;
    QSpinBox* grayThresholdSpin_ = nullptr;
    QSpinBox* minGraySpin_ = nullptr;
    QDoubleSpinBox* binaryThresholdSpin_ = nullptr;
    QDoubleSpinBox* selectionThresholdSpin_ = nullptr;
    QDoubleSpinBox* stripeWidthSpin_ = nullptr;
    QDoubleSpinBox* matchDistanceSpin_ = nullptr;
    QCheckBox* removeEndpointsCheck_ = nullptr;
    QSpinBox* removeEndpointCountSpin_ = nullptr;
};

} // namespace htmsr::app
