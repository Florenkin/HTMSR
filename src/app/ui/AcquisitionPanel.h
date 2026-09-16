#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <QString>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QFormLayout;
class QLineEdit;
class QObject;
class QPushButton;
class QSpinBox;

namespace htmsr::app {

class AcquisitionPanel final : public QWidget {
    Q_OBJECT

public:
    explicit AcquisitionPanel(QWidget* parent = nullptr);

    /*
        函数功能：将枚举到的相机设备显示到左右相机下拉框
        输入：
            devices：在线采集设备列表
        输出：
            无（函数会刷新 UI 控件内容）
    */
    void setDevices(const std::vector<CameraDeviceInfo>& devices);

    /*
        函数功能：从采集面板读取一次双相机采集任务配置
        输入：
            无
        输出：
            返回值：双相机 id、帧数、输出目录、曝光、增益和触发配置
    */
    StereoCameraConfig stereoCameraConfig() const;

    /*
        函数功能：从采集面板读取联动扫描配置
        输入：
            无
        输出：
            返回值：包含双相机采集参数、振镜串口参数和重标开关的联动配置
    */
    IntegratedScanConfig integratedScanConfig() const;
    AppProjectConfig projectConfig() const;
    CalibrationInput calibrationInput() const;
    ReconstructionInput reconstructionInput(const CalibrationResult& calibration) const;
    void setProjectConfig(const AppProjectConfig& config);
    void setCalibrationDirectories(const std::string& leftDirectory, const std::string& rightDirectory);
    void setCalibrationFile(const std::string& calibrationFile);
    void setReconstructionDirectories(const std::string& leftDirectory, const std::string& rightDirectory);

    /*
        函数功能：设置采集面板忙碌状态
        输入：
            busy：是否正在执行采集任务
        输出：
            无（函数会启用或禁用关键按钮）
    */
    void setBusy(bool busy);

    // 更新采集状态提示文本。
    void setStatusText(const QString& text);
    // 更新工作流结果摘要。
    void setResultSummary(const QString& text);
    QWidget* rawGalvoCommandWidget() const;
    void setCalibrationCaptureState(bool active, int capturedFrameCount);
    void setReconstructionCaptureReady(bool ready);
    // 重建前回读设备角度后，刷新显示并以实际角度计算采集帧数。
    void setFrameCountFromDevice(double stepAngleDeg, int totalRotationAngleDeg);

signals:
    void refreshDevicesRequested();
    void offlineCalibrationRequested();
    void startCalibrationCaptureRequested();
    void captureCalibrationFrameRequested();
    void calibrateCapturedFramesRequested();
    void saveCalibrationResultRequested();
    void loadCalibrationResultRequested();
    void finishCalibrationCaptureRequested();
    void startReconstructionCaptureRequested();
    void reconstructCapturedFramesRequested();
    void sendRawGalvoCommandRequested(const QString& commandText);
    void cameraConfigChanged();
    void galvoConfigChanged();
    void galvoMotionParametersChanged();

protected:
    // 在相机指令输入框中拦截上下键，实现当前窗口内的历史命令浏览。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLineEdit* createPathRow(QFormLayout* form, const QString& label, bool directory);
    QSpinBox* createSpinRow(int min, int max, int value);
    QDoubleSpinBox* createDoubleSpinRow(double min, double max, double value);
    void updateActionButtons();
    void rememberRawGalvoCommand();
    void navigateRawGalvoCommandHistory(int direction);
    void resetRawGalvoCommandHistoryNavigation();
    void updateDerivedFrameCount();

    QComboBox* leftDeviceCombo_ = nullptr;
    QComboBox* rightDeviceCombo_ = nullptr;
    QLineEdit* galvoDeviceEdit_ = nullptr;
    QComboBox* reconstructionCaptureModeCombo_ = nullptr;
    QSpinBox* triggerLineSpin_ = nullptr;
    QLineEdit* frameCountEdit_ = nullptr;
    int derivedFrameCount_ = 2000;
    QLineEdit* outputDirectoryEdit_ = nullptr;
    QDoubleSpinBox* exposureTimeSpin_ = nullptr;
    QDoubleSpinBox* gainSpin_ = nullptr;
    QLineEdit* leftCalibrationEdit_ = nullptr;
    QLineEdit* rightCalibrationEdit_ = nullptr;
    QLineEdit* leftReconstructionEdit_ = nullptr;
    QLineEdit* rightReconstructionEdit_ = nullptr;
    QLineEdit* calibrationFileEdit_ = nullptr;
    QSpinBox* boardWidthSpin_ = nullptr;
    QSpinBox* boardHeightSpin_ = nullptr;
    QDoubleSpinBox* squareWidthSpin_ = nullptr;
    QDoubleSpinBox* squareHeightSpin_ = nullptr;
    QLineEdit* galvoPortEdit_ = nullptr;
    QDoubleSpinBox* galvoTotalRotationAngleSpin_ = nullptr;
    QDoubleSpinBox* galvoStepAngleSpin_ = nullptr;
    QSpinBox* galvoForwardSpeedSpin_ = nullptr;
    QSpinBox* galvoReverseSpeedSpin_ = nullptr;
    QSpinBox* galvoLaserDutySpin_ = nullptr;
    QLineEdit* rawGalvoCommandEdit_ = nullptr;
    QPushButton* rawGalvoCommandButton_ = nullptr;
    QWidget* rawGalvoCommandWidget_ = nullptr;
    QStringList rawGalvoCommandHistory_;
    QString rawGalvoCommandDraft_;
    int rawGalvoCommandHistoryIndex_ = 0;
    bool rawGalvoCommandHistoryBrowsing_ = false;
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

    QPushButton* refreshButton_ = nullptr;
    QPushButton* offlineCalibrationButton_ = nullptr;
    QPushButton* startCalibrationCaptureButton_ = nullptr;
    QPushButton* captureCalibrationFrameButton_ = nullptr;
    QPushButton* calibrateCapturedFramesButton_ = nullptr;
    QPushButton* saveCalibrationResultButton_ = nullptr;
    QPushButton* loadCalibrationResultButton_ = nullptr;
    QPushButton* finishCalibrationCaptureButton_ = nullptr;
    QPushButton* startReconstructionCaptureButton_ = nullptr;
    QPushButton* reconstructCapturedFramesButton_ = nullptr;

    bool busy_ = false;
    bool calibrationCaptureActive_ = false;
    int calibrationCapturedFrameCount_ = 0;
    bool reconstructionCaptureReady_ = false;
};

} // namespace htmsr::app
