#pragma once
#include "core/Cancellation.h"

#include "app/services/AcquisitionService.h"
#include "app/services/AppConfigService.h"
#include "app/services/CalibrationCaptureSessionService.h"
#include "app/services/LaserExtractionStorage.h"
#include "app/services/QtLogSink.h"
#include "app/services/ReconstructionCaptureSessionService.h"
#include "core/CalibrationService.h"
#include "core/ReconstructionService.h"

#include <QFutureWatcher>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <memory>
#include <utility>

class QProgressBar;
class QLabel;
class QCloseEvent;

namespace htmsr::app {

struct SerialCommandPackSendResult {
    bool success = false;
    QString message;
    bool laserStateChanged = false;
    bool laserEnabled = false;
    QStringList receivedCommands;
};

struct GalvoMotionVerificationResult {
    bool success = false;
    QString message;
    double actualStepAngleDeg = 0.0;
    int actualTotalAngleDeg = 0;
    QString portName;
};

struct ReconstructionTaskResult {
    ReconstructionResult reconstruction;
    LaserExtractionImageResult laserExtractionImages;
};

class AcquisitionPanel;
class CaptureReviewWidget;
class ImageViewWidget;
class LogPanel;
class PointCloudViewWidget;
class SerialCommandPackWidget;
class ConfigAutoSave;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /*
        函数功能：构造主窗口，初始化 Dock 区域、中央视图、日志、配置和采集入口
        输入：
            parent：Qt 父窗口
        输出：
            无
    */
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // 执行双目标定任务，后台线程调用 CalibrationService。
    void runCalibration();
    // 从磁盘加载已有标定文件。
    void loadCalibration();
    void exportCalibration();
    void exportReconstruction();
    // 刷新在线采集设备列表。
    void refreshAcquisitionDevices();
    void captureCalibrationFrame();
    void calibrateCapturedFrames();
    void loadCalibrationFile(const QString& file);
    void finishCalibrationCapture();
    void startReconstructionCapture();
    void onReconstructionGalvoPreflightFinished();
    void reconstructCapturedFrames();
    void sendRawGalvoCommand(const QString& commandText);
    void sendSerialCommandPack(const QString& name, const QString& content);
    void onSerialCommandPackFinished();
    void applyGalvoMotionParameters();
    void onGalvoMotionParametersApplied();
    // 标定后台任务结束后的 UI 回调。
    void onCalibrationFinished();
    // 重建后台任务结束后的 UI 回调。
    void onReconstructionFinished();
    void onCalibrationFrameCaptured();
    void onReconstructionCaptureFinished();
    void onCaptureReviewResultChanged();

private:
    // 构建右侧在线工作流面板和底部日志面板。
    void buildDocks();
    // 构建中央点云/图像显示标签页。
    void buildCentralView();
    void updateResultAvailability();
    // 更新任务忙碌状态和底部进度条。
    void setBusy(bool busy, const QString& text, bool keepLivePreview = false);
    // 将后台采集到的左右帧安全投递到主线程，用于实时刷新双目实时窗口。
    void enqueueLivePreview(const FramePair& frame);
    // 使用相机原始左右帧刷新双目实时窗口。
    void setLiveStereoImages(const cv::Mat& leftImage, const cv::Mat& rightImage);
    // 根据双目实时界面刷新频率更新帧率显示。
    void updateLiveFps();
    // 重置双目实时帧率统计窗口。
    void resetLiveFps();
    // 将最近一次采集结果刷新到“采集”页。
    void setCaptureReviewResult(const AcquisitionSessionResult& result);
    // 启动空闲状态下的双目相机实时预览。
    void startLivePreview();
    // 停止双目相机实时预览并释放相机。
    void stopLivePreview();
    // 相机参数变化后重启实时预览。
    void restartLivePreview();
    // 后台实时预览循环，持续抓取左右相机原始图像。
    void runLivePreviewLoop(StereoCameraConfig config);
    // 标定采集会话期间复用已打开的相机句柄持续刷新实时预览。
    void runCalibrationCapturePreviewLoop();
    void refreshLaserSwitchPreview(const IntegratedScanConfig& config);
    void beginReconstructionCapture(const IntegratedScanConfig& config);
    void updateGalvoStatusBar(const IntegratedScanConfig& config, const QString& note = QString());
    // 根据在线采集保存目录拼接统一的标定结果文件夹。
    QString calibrationResultsDirectory() const;
    // 生成一次新的默认标定结果文件路径。
    QString defaultCalibrationFilePath() const;

    ConfigAutoSave* configAutoSave_ = nullptr;
    AppConfigService configService_;
    AcquisitionService acquisitionService_;
    CalibrationCaptureSessionService calibrationCaptureSessionService_;
    ReconstructionCaptureSessionService reconstructionCaptureSessionService_;
    CalibrationService calibrationService_;
    ReconstructionService reconstructionService_;
    QtLogSink* logSink_ = nullptr;

    AcquisitionPanel* acquisitionPanel_ = nullptr;
    LogPanel* logPanel_ = nullptr;
    PointCloudViewWidget* pointCloudView_ = nullptr;
    CaptureReviewWidget* captureReviewWidget_ = nullptr;
    CaptureReviewWidget* laserExtractionReviewWidget_ = nullptr;
    SerialCommandPackWidget* serialCommandPackWidget_ = nullptr;
    ImageViewWidget* liveLeftImageView_ = nullptr;
    ImageViewWidget* liveRightImageView_ = nullptr;
    QLabel* liveFpsLabel_ = nullptr;
    QLabel* galvoStatusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;

    CalibrationResult calibration_;
    CalibrationInput pendingCalibrationInput_;
    ReconstructionResult reconstruction_;
    AcquisitionSessionResult acquisition_;
    AcquisitionSessionResult calibrationCapture_;
    AcquisitionSessionResult reconstructionCapture_;
    bool galvoLaserEnabled_ = false;
    bool busy_ = false;
    CancellationToken shutdownCancellation_;
    std::atomic_bool shuttingDown_ = false;
    std::atomic_bool livePreviewStopRequested_ = false;
    std::chrono::steady_clock::time_point liveFpsWindowStart_;
    int liveFpsFrameCount_ = 0;
    QFutureWatcher<CalibrationResult> calibrationWatcher_;
    QFutureWatcher<ReconstructionTaskResult> reconstructionWatcher_;
    QFutureWatcher<AcquisitionSessionResult> calibrationFrameWatcher_;
    QFutureWatcher<AcquisitionSessionResult> reconstructionCaptureWatcher_;
    QFutureWatcher<GalvoMotionVerificationResult> reconstructionGalvoPreflightWatcher_;
    QFutureWatcher<void> livePreviewWatcher_;
    QFutureWatcher<GalvoMotionVerificationResult> galvoMotionParametersWatcher_;
    QFutureWatcher<SerialCommandPackSendResult> serialCommandPackWatcher_;
    std::shared_ptr<std::atomic_bool> serialCommandPackStopRequested_ = std::make_shared<std::atomic_bool>(false);
    IntegratedScanConfig pendingReconstructionCaptureConfig_;
    bool galvoMotionParametersPending_ = false;
};

} // namespace htmsr::app
