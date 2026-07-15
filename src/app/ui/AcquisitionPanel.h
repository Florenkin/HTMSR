#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
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

signals:
    void refreshDevicesRequested();
    void captureRequested();
    void autoCalibrationRequested();
    void scanAndReconstructRequested();

private:
    QComboBox* leftDeviceCombo_ = nullptr;
    QComboBox* rightDeviceCombo_ = nullptr;
    QCheckBox* useMockProviderCheck_ = nullptr;
    QSpinBox* frameCountSpin_ = nullptr;
    QLineEdit* outputDirectoryEdit_ = nullptr;
    QDoubleSpinBox* exposureTimeSpin_ = nullptr;
    QDoubleSpinBox* gainSpin_ = nullptr;
    QCheckBox* hardwareTriggerCheck_ = nullptr;
    QSpinBox* triggerLineSpin_ = nullptr;
    QSpinBox* timeoutSpin_ = nullptr;

    QLineEdit* galvoPortEdit_ = nullptr;
    QSpinBox* galvoBaudRateSpin_ = nullptr;
    QSpinBox* galvoCommandTimeoutSpin_ = nullptr;
    QComboBox* galvoSyncModeCombo_ = nullptr;
    QComboBox* galvoDirectionCombo_ = nullptr;
    QSpinBox* galvoCaptureIntervalSpin_ = nullptr;
    QSpinBox* galvoContinuousWaitSpin_ = nullptr;
    QDoubleSpinBox* galvoStepAngleSpin_ = nullptr;
    QSpinBox* galvoAutoRotationAngleSpin_ = nullptr;
    QSpinBox* galvoForwardSpeedSpin_ = nullptr;
    QSpinBox* galvoReverseSpeedSpin_ = nullptr;
    QSpinBox* galvoLaserDutySpin_ = nullptr;
    QDoubleSpinBox* galvoVoltageRangeSpin_ = nullptr;
    QCheckBox* forceRecalibrationCheck_ = nullptr;

    QLineEdit* statusEdit_ = nullptr;
    QLineEdit* summaryEdit_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* captureButton_ = nullptr;
    QPushButton* autoCalibrationButton_ = nullptr;
    QPushButton* scanReconstructButton_ = nullptr;
};

} // namespace htmsr::app
