#pragma once

#include "app/services/AcquisitionService.h"
#include "app/services/AppConfigService.h"
#include "app/services/CalibrationCaptureSessionService.h"
#include "app/services/IntegratedCalibrationCaptureService.h"
#include "app/services/IntegratedScanService.h"
#include "app/services/QtLogSink.h"
#include "app/services/ReconstructionCaptureSessionService.h"
#include "core/CalibrationService.h"
#include "core/PointCloudService.h"
#include "core/ReconstructionService.h"

#include <QFutureWatcher>
#include <QMainWindow>

class QProgressBar;
class QLabel;
class QTreeWidget;

namespace htmsr::app {

class AcquisitionPanel;
class ImageViewWidget;
class LogPanel;
class PointCloudViewWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /*
        函数功能：构造主窗口，初始化菜单、工具栏、Dock 区域、中央视图、日志、配置和采集入口
        输入：
            parent：Qt 父窗口
        输出：
            无
    */
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    // 执行双目标定任务，后台线程调用 CalibrationService。
    void runCalibration();
    // 从磁盘加载已有标定文件。
    void loadCalibration();
    // 执行离线三维重建任务，后台线程调用 ReconstructionService。
    void runReconstruction();
    // 导出当前点云为 txt 文件。
    void exportTxt();
    // 导出当前点云为 pcd 文件。
    void exportPcd();
    // 保存当前项目参数到 QSettings。
    void saveProjectSettings();
    // 刷新在线采集设备列表。
    void refreshAcquisitionDevices();
    // 执行在线采集保存任务。
    void runAcquisition();
    // 执行一键自动标定任务。
    void runAutoCalibration();
    // 执行一键扫描重建任务。
    void runScanAndReconstruct();
    void startCalibrationCapture();
    void captureCalibrationFrame();
    void calibrateCapturedFrames();
    void saveCalibrationResult();
    void loadCalibrationResult();
    void finishCalibrationCapture();
    void startReconstructionCapture();
    void reconstructCapturedFrames();
    void returnGalvoToCenter();
    void sendRawGalvoCommand(const QString& commandText);
    // 标定后台任务结束后的 UI 回调。
    void onCalibrationFinished();
    // 重建后台任务结束后的 UI 回调。
    void onReconstructionFinished();
    // 在线采集后台任务结束后的 UI 回调。
    void onAcquisitionFinished();
    // 一键自动标定后台任务结束后的 UI 回调。
    void onAutoCalibrationFinished();
    // 一键扫描重建后台任务结束后的 UI 回调。
    void onScanAndReconstructFinished();
    void onCalibrationCaptureStarted();
    void onCalibrationFrameCaptured();
    void onReconstructionCaptureFinished();

private:
    // 构建顶部菜单栏。
    void buildMenus();
    // 构建顶部工具栏。
    void buildToolBar();
    // 构建左侧资源树、右侧参数/采集面板和底部日志面板。
    void buildDocks();
    // 构建中央点云/图像显示标签页。
    void buildCentralView();
    // 刷新左侧项目资源树。
    void refreshProjectTree();
    // 更新任务忙碌状态和底部进度条。
    void setBusy(bool busy, const QString& text);
    // 将后台采集到的左右帧安全投递到主线程，用于实时刷新左图、右图和调试图。
    void enqueueLivePreview(const FramePair& frame);
    // 使用相机原始左右帧刷新双目实时窗口。
    void setLiveStereoImages(const cv::Mat& leftImage, const cv::Mat& rightImage);
    void refreshLaserSwitchPreview(const IntegratedScanConfig& config);
    void updateGalvoStatusBar(const IntegratedScanConfig& config, const QString& note = QString());
    // 根据在线采集保存目录拼接统一的标定结果文件夹。
    QString calibrationResultsDirectory() const;
    // 根据输出目录拼接默认导出文件路径。
    QString outputPath(const QString& filename) const;

    AppConfigService configService_;
    AcquisitionService acquisitionService_;
    CalibrationCaptureSessionService calibrationCaptureSessionService_;
    IntegratedCalibrationCaptureService integratedCalibrationCaptureService_;
    IntegratedScanService integratedScanService_;
    ReconstructionCaptureSessionService reconstructionCaptureSessionService_;
    CalibrationService calibrationService_;
    ReconstructionService reconstructionService_;
    PointCloudService pointCloudService_;
    QtLogSink* logSink_ = nullptr;

    AcquisitionPanel* acquisitionPanel_ = nullptr;
    LogPanel* logPanel_ = nullptr;
    PointCloudViewWidget* pointCloudView_ = nullptr;
    ImageViewWidget* liveLeftImageView_ = nullptr;
    ImageViewWidget* liveRightImageView_ = nullptr;
    ImageViewWidget* leftImageView_ = nullptr;
    ImageViewWidget* rightImageView_ = nullptr;
    ImageViewWidget* debugImageView_ = nullptr;
    QTreeWidget* projectTree_ = nullptr;
    QLabel* galvoStatusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;

    CalibrationResult calibration_;
    ReconstructionResult reconstruction_;
    AcquisitionSessionResult acquisition_;
    AcquisitionSessionResult calibrationCapture_;
    AcquisitionSessionResult reconstructionCapture_;
    IntegratedWorkflowResult autoCalibrationWorkflow_;
    IntegratedWorkflowResult scanWorkflow_;
    bool autoExportReconstructionOnFinish_ = false;
    bool galvoLaserEnabled_ = false;
    QFutureWatcher<CalibrationResult> calibrationWatcher_;
    QFutureWatcher<ReconstructionResult> reconstructionWatcher_;
    QFutureWatcher<AcquisitionSessionResult> acquisitionWatcher_;
    QFutureWatcher<CalibrationCaptureSessionState> calibrationCaptureStartWatcher_;
    QFutureWatcher<AcquisitionSessionResult> calibrationFrameWatcher_;
    QFutureWatcher<AcquisitionSessionResult> reconstructionCaptureWatcher_;
    QFutureWatcher<IntegratedWorkflowResult> autoCalibrationWatcher_;
    QFutureWatcher<IntegratedWorkflowResult> scanWorkflowWatcher_;
};

} // namespace htmsr::app
