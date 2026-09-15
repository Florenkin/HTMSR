#include "app/ui/MainWindow.h"

#include "app/acquisition/GalvoController.h"
#if HTMSR_WITH_HIK_CAMERA
#include "app/acquisition/HikCameraDevice.h"
#endif
#include "app/ui/AcquisitionPanel.h"
#include "app/ui/CaptureReviewWidget.h"
#include "app/ui/ImageViewWidget.h"
#include "app/ui/LogPanel.h"
#include "app/ui/PointCloudViewWidget.h"
#include "app/ui/SerialCommandPackWidget.h"
#include "core/LaserExtractionService.h"
#include "core/Logger.h"

#include <opencv2/core.hpp>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QException>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFuture>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

class WorkerException final : public QException {
public:
    explicit WorkerException(std::string message)
        : message_(std::move(message))
    {
    }

    void raise() const override
    {
        throw *this;
    }

    WorkerException* clone() const override
    {
        return new WorkerException(*this);
    }

    const char* what() const noexcept override
    {
        return message_.c_str();
    }

private:
    std::string message_;
};

template <typename Function>
auto runWorkerTask(const char* taskName, Function&& function) -> decltype(function())
{
    try {
        return function();
    } catch (const WorkerException&) {
        throw;
    } catch (const std::exception& ex) {
        throw WorkerException(ex.what());
    } catch (...) {
        throw WorkerException(std::string(taskName) + " failed with an unknown non-standard exception.");
    }
}

htmsr::ReconstructionResult failedReconstructionResult(const std::string& message)
{
    htmsr::ReconstructionResult result;
    result.success = false;
    result.message = message;
    return result;
}

htmsr::ReconstructionResult runReconstructionTask(const htmsr::ReconstructionInput& input)
{
    try {
        htmsr::ReconstructionService service;
        return service.reconstruct(input);
    } catch (const cv::Exception& ex) {
        return failedReconstructionResult(std::string("OpenCV 重建异常：") + ex.what());
    } catch (const std::exception& ex) {
        return failedReconstructionResult(ex.what());
    } catch (...) {
        return failedReconstructionResult("重建失败：发生未知异常。");
    }
}

} // namespace

namespace htmsr::app {

namespace {

CameraDeviceInfo findSelectedCameraDevice(const std::vector<CameraDeviceInfo>& devices, const std::string& deviceId)
{
    for (const auto& device : devices) {
        if (device.id == deviceId) {
            return device;
        }
    }
    throw std::runtime_error("Camera device was not found, id=" + deviceId);
}

QString directionText(GalvoScanDirection direction)
{
    return direction == GalvoScanDirection::Forward ? QString::fromUtf8("正向") : QString::fromUtf8("反向");
}

QString syncModeText(GalvoSyncMode mode)
{
    return mode == GalvoSyncMode::Sync ? QString::fromUtf8("硬触发") : QString::fromUtf8("软件同步");
}

std::vector<unsigned char> parseHexCommandText(const QString& commandText)
{
    QString normalized = commandText.trimmed();
    normalized.replace(",", " ");
    normalized.replace(";", " ");
    normalized.replace("\r", " ");
    normalized.replace("\n", " ");
    normalized.replace("\t", " ");

    if (normalized.isEmpty()) {
        throw std::runtime_error("相机指令不能为空。");
    }

    QStringList tokens = normalized.split(' ', Qt::SkipEmptyParts);
    if (tokens.size() == 1) {
        QString compact = tokens.front().trimmed();
        if (compact.startsWith("0x", Qt::CaseInsensitive)) {
            compact = compact.mid(2);
        }
        if (compact.size() > 2 && compact.size() % 2 == 0) {
            tokens.clear();
            for (int i = 0; i < compact.size(); i += 2) {
                tokens << compact.mid(i, 2);
            }
        }
    }

    std::vector<unsigned char> command;
    command.reserve(static_cast<size_t>(tokens.size()));
    for (const QString& token : tokens) {
        QString cleaned = token.trimmed();
        if (cleaned.startsWith("0x", Qt::CaseInsensitive)) {
            cleaned = cleaned.mid(2);
        }

        bool ok = false;
        const uint value = cleaned.toUInt(&ok, 16);
        if (!ok || value > 0xFFu) {
            throw std::runtime_error(QString::fromUtf8("相机指令包含非法字节：%1").arg(token).toStdString());
        }
        command.push_back(static_cast<unsigned char>(value));
    }

    if (command.empty()) {
        throw std::runtime_error("相机指令不能为空。");
    }
    return command;
}

QString bytesToHexText(const std::vector<unsigned char>& bytes)
{
    QStringList parts;
    for (unsigned char byte : bytes) {
        parts << QString("%1").arg(static_cast<unsigned int>(byte), 2, 16, QChar('0')).toUpper();
    }
    return parts.join(' ');
}

bool shouldRefreshPreviewAfterRawCommand(const std::vector<unsigned char>& command)
{
    if (command.size() < 4) {
        return false;
    }

    const unsigned char operation = command[3];
    return operation == 0x04 || operation == 0x05 || operation == 0x09 ||
        operation == 0x1A || operation == 0x1B;
}

GalvoMotionVerificationResult configureAndVerifyGalvoMotionParameters(const GalvoScanConfig& galvoConfig)
{
    SerialGalvoController controller(galvoConfig);
    try {
        if (!controller.connect()) {
            return {
                false,
                QString::fromUtf8("无法连接振镜串口：%1").arg(QString::fromStdString(galvoConfig.portName))
            };
        }

        const auto reconnectController = [&controller, &galvoConfig]() {
            controller.disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            if (!controller.connect()) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return true;
        };
        const auto queryWithRecovery = [&reconnectController](const auto& query) {
            GalvoCommandResult result;
            for (int attempt = 0; attempt < 4; ++attempt) {
                if (attempt > 0 && !reconnectController()) {
                    result.success = false;
                    result.message = "重新连接振镜串口失败。";
                    std::this_thread::sleep_for(std::chrono::milliseconds(120));
                    continue;
                }

                result = query();
                if (result.success) {
                    return result;
                }
                if (attempt < 3) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(120));
                }
            }
            return result;
        };

        constexpr double kStepAngleToleranceDeg = 0.006;
        QString lastFailure = QString::fromUtf8("参数重新设置后回读验证失败。");
        for (int configureAttempt = 0; configureAttempt < 3; ++configureAttempt) {
            if (configureAttempt > 0 && !reconnectController()) {
                lastFailure = QString::fromUtf8("参数验证失败后，重新连接振镜串口失败：%1")
                    .arg(QString::fromStdString(galvoConfig.portName));
                continue;
            }

            const auto stepSetResult = controller.setStepAngle(galvoConfig.stepAngleDeg);
            if (!stepSetResult.success) {
                lastFailure = QString::fromUtf8("振镜已连接，但重新设置步进角度失败：%1")
                    .arg(QString::fromStdString(stepSetResult.message));
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(80));

            // 两个写入命令之间重新建立串口连接，避免振镜仍在处理步进角度
            // 设置命令时丢弃紧随其后的自动旋转角度设置命令。
            if (!reconnectController()) {
                lastFailure = QString::fromUtf8("步进角度已重新设置，但发送自动旋转角度前重连振镜失败：%1")
                    .arg(QString::fromStdString(galvoConfig.portName));
                continue;
            }

            const auto autoRotationSetResult = controller.setAutoRotationAngle(galvoConfig.autoRotationAngleDeg);
            if (!autoRotationSetResult.success) {
                lastFailure = QString::fromUtf8("振镜已连接，但重新设置自动旋转角度失败：%1")
                    .arg(QString::fromStdString(autoRotationSetResult.message));
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            double actualStepAngle = 0.0;
            const GalvoCommandResult stepQueryResult = queryWithRecovery([&]() {
                return controller.getStepAngle(actualStepAngle);
            });
            if (!stepQueryResult.success) {
                lastFailure = QString::fromUtf8("步进角度已重新设置，但回读验证失败：%1")
                    .arg(QString::fromStdString(stepQueryResult.message));
                continue;
            }

            int actualAutoRotationAngle = 0;
            const GalvoCommandResult autoRotationQueryResult = queryWithRecovery([&]() {
                return controller.getAutoRotationAngle(actualAutoRotationAngle);
            });
            if (!autoRotationQueryResult.success) {
                lastFailure = QString::fromUtf8("自动旋转角度已重新设置，但回读验证失败：%1")
                    .arg(QString::fromStdString(autoRotationQueryResult.message));
                continue;
            }

            if (std::abs(actualStepAngle - galvoConfig.stepAngleDeg) > kStepAngleToleranceDeg) {
                lastFailure = QString::fromUtf8("步进角度重新设置后仍不一致：界面 %1°，设备实际 %2°")
                    .arg(galvoConfig.stepAngleDeg, 0, 'f', 4)
                    .arg(actualStepAngle, 0, 'f', 4);
                continue;
            }
            if (actualAutoRotationAngle != galvoConfig.autoRotationAngleDeg) {
                lastFailure = QString::fromUtf8("自动旋转角度重新设置后仍不一致：界面 %1°，设备实际 %2°")
                    .arg(galvoConfig.autoRotationAngleDeg)
                    .arg(actualAutoRotationAngle);
                continue;
            }

            controller.disconnect();
            return {
                true,
                QString::fromUtf8("振镜已连接，参数重新设置并验证通过：步进 %1°，自动旋转 %2°")
                    .arg(actualStepAngle, 0, 'f', 4)
                    .arg(actualAutoRotationAngle),
                actualStepAngle,
                actualAutoRotationAngle,
                QString::fromStdString(galvoConfig.portName)
            };
        }

        controller.disconnect();
        return { false, lastFailure };
    } catch (const std::exception& ex) {
        controller.disconnect();
        return { false, QString::fromUtf8("振镜连接或参数回读异常：%1").arg(QString::fromUtf8(ex.what())) };
    } catch (...) {
        controller.disconnect();
        return { false, QString::fromUtf8("振镜连接或参数回读发生未知异常。") };
    }
}

} // namespace

/*
    函数功能：构造主窗口，初始化日志桥接、中央视图、Dock、菜单、工具栏和后台任务连接
    输入：
        parent：Qt 父控件
    输出：
        无（构造后主窗口进入可交互状态）
*/
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("HTMSR");
    resize(1440, 920);
    setStyleSheet(
        "QMainWindow { background: #d9d9d9; }"
        "QMenuBar, QToolBar { background: #efefef; border-bottom: 1px solid #b8b8b8; }"
        "QDockWidget::title { background: #d6d6d6; padding: 4px; border: 1px solid #b8b8b8; }"
        "QTabWidget::pane { border: 1px solid #b8b8b8; background: #f4f4f4; }"
        "QTabBar::tab { background: #e8e8e8; padding: 4px 12px; border: 1px solid #b8b8b8; }"
        "QTabBar::tab:selected { background: #ffffff; }"
        "QTableWidget, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #ffffff; border: 1px solid #c0c0c0; }"
        "QPushButton { padding: 3px 8px; }");

    logSink_ = new QtLogSink(this);
    connect(logSink_, &QtLogSink::messageReceived, this, [this](const LogMessage& message) {
        logPanel_->appendMessage(message);
    }, Qt::QueuedConnection);

    // 按参考界面布局依次构建中央视图、Dock 区域、菜单和工具栏。
    buildCentralView();
    buildDocks();
    buildMenus();
    buildToolBar();

    progressBar_ = new QProgressBar;
    progressBar_->setMaximumWidth(180);
    progressBar_->setValue(0);
    galvoStatusLabel_ = new QLabel;
    galvoStatusLabel_->setMinimumWidth(520);
    statusBar()->addPermanentWidget(galvoStatusLabel_, 1);
    statusBar()->addPermanentWidget(progressBar_);

    acquisitionPanel_->setProjectConfig(configService_.load());
    refreshProjectTree();

    connect(&calibrationWatcher_, &QFutureWatcher<CalibrationResult>::finished, this, &MainWindow::onCalibrationFinished);
    connect(&reconstructionWatcher_, &QFutureWatcher<ReconstructionResult>::finished, this, &MainWindow::onReconstructionFinished);
    connect(&acquisitionWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onAcquisitionFinished);
    connect(&calibrationCaptureStartWatcher_, &QFutureWatcher<CalibrationCaptureSessionState>::finished, this, &MainWindow::onCalibrationCaptureStarted);
    connect(&calibrationFrameWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onCalibrationFrameCaptured);
    connect(&reconstructionCaptureWatcher_, &QFutureWatcher<AcquisitionSessionResult>::finished, this, &MainWindow::onReconstructionCaptureFinished);
    connect(&autoCalibrationWatcher_, &QFutureWatcher<IntegratedWorkflowResult>::finished, this, &MainWindow::onAutoCalibrationFinished);
    connect(&scanWorkflowWatcher_, &QFutureWatcher<IntegratedWorkflowResult>::finished, this, &MainWindow::onScanAndReconstructFinished);
    connect(&reconstructionGalvoPreflightWatcher_, &QFutureWatcher<GalvoMotionVerificationResult>::finished, this, &MainWindow::onReconstructionGalvoPreflightFinished);
    connect(&galvoMotionParametersWatcher_, &QFutureWatcher<GalvoMotionVerificationResult>::finished, this, &MainWindow::onGalvoMotionParametersApplied);
    connect(&serialCommandPackWatcher_, &QFutureWatcher<SerialCommandPackSendResult>::finished, this, &MainWindow::onSerialCommandPackFinished);
    connect(serialCommandPackWidget_, &SerialCommandPackWidget::sendRequested, this, &MainWindow::sendSerialCommandPack);
    connect(acquisitionPanel_, &AcquisitionPanel::refreshDevicesRequested, this, &MainWindow::refreshAcquisitionDevices);
    connect(acquisitionPanel_, &AcquisitionPanel::offlineCalibrationRequested, this, &MainWindow::runCalibration);
    connect(acquisitionPanel_, &AcquisitionPanel::startCalibrationCaptureRequested, this, &MainWindow::startCalibrationCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::captureCalibrationFrameRequested, this, &MainWindow::captureCalibrationFrame);
    connect(acquisitionPanel_, &AcquisitionPanel::calibrateCapturedFramesRequested, this, &MainWindow::calibrateCapturedFrames);
    connect(acquisitionPanel_, &AcquisitionPanel::saveCalibrationResultRequested, this, &MainWindow::saveCalibrationResult);
    connect(acquisitionPanel_, &AcquisitionPanel::loadCalibrationResultRequested, this, &MainWindow::loadCalibrationResult);
    connect(acquisitionPanel_, &AcquisitionPanel::finishCalibrationCaptureRequested, this, &MainWindow::finishCalibrationCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::startReconstructionCaptureRequested, this, &MainWindow::startReconstructionCapture);
    connect(acquisitionPanel_, &AcquisitionPanel::reconstructCapturedFramesRequested, this, &MainWindow::reconstructCapturedFrames);
    connect(acquisitionPanel_, &AcquisitionPanel::sendRawGalvoCommandRequested, this, &MainWindow::sendRawGalvoCommand);
    connect(acquisitionPanel_, &AcquisitionPanel::cameraConfigChanged, this, &MainWindow::restartLivePreview);
    connect(acquisitionPanel_, &AcquisitionPanel::galvoConfigChanged, this, [this]() {
        updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig());
    });
    connect(acquisitionPanel_, &AcquisitionPanel::galvoMotionParametersChanged, this, &MainWindow::applyGalvoMotionParameters);

    refreshAcquisitionDevices();
    startLivePreview();
    updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig(), QString::fromUtf8("未连接"));
    Logger::instance().info("App", "HTMSR started.");
}

// 析构时等待后台任务退出，避免窗口关闭后仍有线程访问已经销毁的 UI 对象。
MainWindow::~MainWindow()
{
    shuttingDown_.store(true);
    disconnect(logSink_, nullptr, this, nullptr);
    disconnect(&calibrationWatcher_, nullptr, this, nullptr);
    disconnect(&reconstructionWatcher_, nullptr, this, nullptr);
    disconnect(&acquisitionWatcher_, nullptr, this, nullptr);
    disconnect(&calibrationCaptureStartWatcher_, nullptr, this, nullptr);
    disconnect(&calibrationFrameWatcher_, nullptr, this, nullptr);
    disconnect(&reconstructionCaptureWatcher_, nullptr, this, nullptr);
    disconnect(&autoCalibrationWatcher_, nullptr, this, nullptr);
    disconnect(&scanWorkflowWatcher_, nullptr, this, nullptr);
    disconnect(&reconstructionGalvoPreflightWatcher_, nullptr, this, nullptr);
    disconnect(&livePreviewWatcher_, nullptr, this, nullptr);
    disconnect(&galvoMotionParametersWatcher_, nullptr, this, nullptr);
    disconnect(&serialCommandPackWatcher_, nullptr, this, nullptr);
    serialCommandPackStopRequested_->store(true);
    stopLivePreview();
    if (calibrationWatcher_.isRunning()) {
        calibrationWatcher_.cancel();
        calibrationWatcher_.waitForFinished();
    }
    if (reconstructionWatcher_.isRunning()) {
        reconstructionWatcher_.cancel();
        reconstructionWatcher_.waitForFinished();
    }
    if (acquisitionWatcher_.isRunning()) {
        acquisitionWatcher_.cancel();
        acquisitionWatcher_.waitForFinished();
    }
    if (calibrationCaptureStartWatcher_.isRunning()) {
        calibrationCaptureStartWatcher_.cancel();
        calibrationCaptureStartWatcher_.waitForFinished();
    }
    if (calibrationFrameWatcher_.isRunning()) {
        calibrationFrameWatcher_.cancel();
        calibrationFrameWatcher_.waitForFinished();
    }
    if (reconstructionCaptureWatcher_.isRunning()) {
        reconstructionCaptureWatcher_.cancel();
        reconstructionCaptureWatcher_.waitForFinished();
    }
    if (reconstructionGalvoPreflightWatcher_.isRunning()) {
        reconstructionGalvoPreflightWatcher_.cancel();
        reconstructionGalvoPreflightWatcher_.waitForFinished();
    }
    if (autoCalibrationWatcher_.isRunning()) {
        autoCalibrationWatcher_.cancel();
        autoCalibrationWatcher_.waitForFinished();
    }
    if (scanWorkflowWatcher_.isRunning()) {
        scanWorkflowWatcher_.cancel();
        scanWorkflowWatcher_.waitForFinished();
    }
    if (galvoMotionParametersWatcher_.isRunning()) {
        galvoMotionParametersWatcher_.cancel();
        galvoMotionParametersWatcher_.waitForFinished();
    }
    if (serialCommandPackWatcher_.isRunning()) {
        serialCommandPackWatcher_.waitForFinished();
    }
    calibrationCaptureSessionService_.finish();
    QCoreApplication::removePostedEvents(this);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    shuttingDown_.store(true);
    serialCommandPackStopRequested_->store(true);
    stopLivePreview();
    QCoreApplication::removePostedEvents(this);
    QMainWindow::closeEvent(event);
}

/*
    函数功能：从参数面板读取标定参数并在后台启动双目标定任务
    输入：
        无
    输出：
        无（函数会切换忙碌状态并启动异步标定）
*/
void MainWindow::runCalibration()
{
    if (calibrationWatcher_.isRunning()) {
        return;
    }

    CalibrationInput input = acquisitionPanel_->calibrationInput();
    if (QString::fromStdString(input.outputFile).trimmed().isEmpty()) {
        const QString filePath = defaultCalibrationFilePath();
        input.outputFile = filePath.toStdString();
        acquisitionPanel_->setCalibrationFile(input.outputFile);
    }

    setBusy(true, QString::fromUtf8("标定中..."), true);
    calibrationWatcher_.setFuture(QtConcurrent::run([input]() {
        return runWorkerTask("Calibration", [input]() {
            CalibrationService service;
            return service.calibrate(input);
        });
    }));
}

/*
    函数功能：从磁盘加载已有的双目标定文件
    输入：
        无
    输出：
        无（加载成功后更新内存中的标定结果，失败时弹出提示）
*/
void MainWindow::loadCalibration()
{
    const QString file = QFileDialog::getOpenFileName(this, QString::fromUtf8("加载标定文件"), QString(), QString::fromUtf8("YAML (*.yml *.yaml);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    CalibrationResult loaded;
    if (calibrationService_.loadCalibration(file.toStdString(), loaded)) {
        calibration_ = loaded;
        Logger::instance().info("App", "Calibration loaded from UI.");
    } else {
        QMessageBox::warning(this, QString::fromUtf8("加载失败"), QString::fromUtf8("标定文件无效或无法读取。"));
    }
}

/*
    函数功能：使用当前参数和标定结果在后台启动离线重建任务
    输入：
        无
    输出：
        无（函数会校验标定结果并启动异步重建）
*/
void MainWindow::runReconstruction()
{
    try {
        if (reconstructionWatcher_.isRunning()) {
            return;
        }

        if (!calibration_.isValid()) {
            const auto file = acquisitionPanel_->calibrationInput().outputFile;
            calibrationService_.loadCalibration(file, calibration_);
        }
        if (!calibration_.isValid()) {
            QMessageBox::warning(this, QString::fromUtf8("缺少标定"), QString::fromUtf8("请先完成标定或加载有效标定文件。"));
            return;
        }

        const ReconstructionInput input = acquisitionPanel_->reconstructionInput(calibration_);
        autoExportReconstructionOnFinish_ = false;
        setBusy(true, QString::fromUtf8("重建中..."), true);
        reconstructionWatcher_.setFuture(QtConcurrent::run(runReconstructionTask, input));
    } catch (const cv::Exception& ex) {
        autoExportReconstructionOnFinish_ = false;
        setBusy(false, QString());
        Logger::instance().error("Reconstruction", std::string("Failed to start reconstruction: ") + ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("启动重建时发生 OpenCV 异常：%1").arg(QString::fromStdString(ex.what())));
    } catch (const std::exception& ex) {
        autoExportReconstructionOnFinish_ = false;
        setBusy(false, QString());
        Logger::instance().error("Reconstruction", std::string("Failed to start reconstruction: ") + ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromStdString(ex.what()));
    } catch (...) {
        autoExportReconstructionOnFinish_ = false;
        setBusy(false, QString());
        Logger::instance().error("Reconstruction", "Failed to start reconstruction with an unknown exception.");
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("启动重建时发生未知异常。"));
    }
}

/*
    函数功能：导出当前重建结果为 txt 点云文件
    输入：
        无
    输出：
        无（导出成功时写文件，失败时给出错误提示）
*/
void MainWindow::exportTxt()
{
    if (reconstruction_.mergedPoints.empty()) {
        QMessageBox::information(this, QString::fromUtf8("无点云"), QString::fromUtf8("当前没有可导出的点云。"));
        return;
    }

    const QString fileName = "point_cloud_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".txt";
    const QString file = QFileDialog::getSaveFileName(this, QString::fromUtf8("导出 TXT"), outputPath(fileName), QString::fromUtf8("TXT (*.txt);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    try {
        pointCloudService_.saveTxt(file.toStdString(), reconstruction_.mergedPoints);
    } catch (const std::exception& ex) {
        Logger::instance().error("PointCloud", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("导出失败"), QString::fromStdString(ex.what()));
    }
}

/*
    函数功能：导出当前重建结果为 pcd 点云文件
    输入：
        无
    输出：
        无（导出成功时写文件，失败时给出错误提示）
*/
void MainWindow::exportPcd()
{
    if (reconstruction_.mergedPoints.empty()) {
        QMessageBox::information(this, QString::fromUtf8("无点云"), QString::fromUtf8("当前没有可导出的点云。"));
        return;
    }

    const QString fileName = "point_cloud_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".pcd";
    const QString file = QFileDialog::getSaveFileName(this, QString::fromUtf8("导出 PCD"), outputPath(fileName), QString::fromUtf8("PCD (*.pcd);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    try {
        pointCloudService_.savePcd(file.toStdString(), reconstruction_.mergedPoints);
    } catch (const std::exception& ex) {
        Logger::instance().error("PointCloud", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("导出失败"), QString::fromStdString(ex.what()));
    }
}

// 将当前项目参数持久化到配置中。
void MainWindow::saveProjectSettings()
{
    configService_.save(acquisitionPanel_->projectConfig());
    refreshProjectTree();
    Logger::instance().info("App", "Project settings saved.");
}

/*
    函数功能：枚举当前可用的在线采集设备，并刷新采集面板下拉框
    输入：
        无
    输出：
        无（失败时在日志和采集面板中反馈错误）
*/
void MainWindow::refreshAcquisitionDevices()
{
    try {
        const auto devices = acquisitionService_.enumerateDevices();
        acquisitionPanel_->setDevices(devices);
    } catch (const std::exception& ex) {
        Logger::instance().error("Acquisition", ex.what());
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
    }
}

/*
    函数功能：从采集面板读取配置并在后台启动在线采集任务
    输入：
        无
    输出：
        无（函数会切换忙碌状态并启动异步采集）
*/
void MainWindow::runAcquisition()
{
    if (acquisitionWatcher_.isRunning()) {
        return;
    }

    StereoCameraConfig config = acquisitionPanel_->stereoCameraConfig();
    if (config.outputDirectory.empty()) {
        config.outputDirectory = acquisitionPanel_->projectConfig().outputDirectory;
    }

    setBusy(true, QString::fromUtf8("采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("采集中..."));
    acquisitionWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Acquisition", [this, config]() {
            AcquisitionService service;
            return service.capture(config, [this](int, int, const FramePair& frame) {
                enqueueLivePreview(frame);
            });
        });
    }));
}

void MainWindow::runAutoCalibration()
{
    if (autoCalibrationWatcher_.isRunning()) {
        return;
    }

    IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    config.calibrationInput = acquisitionPanel_->calibrationInput();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = acquisitionPanel_->projectConfig().outputDirectory;
    }

    setBusy(true, QString::fromUtf8("自动标定中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("自动标定中..."));
    autoCalibrationWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Auto calibration", [this, config]() {
            return integratedCalibrationCaptureService_.run(config, [this](int, int, const FramePair& frame) {
                enqueueLivePreview(frame);
            });
        });
    }));
}

void MainWindow::runScanAndReconstruct()
{
    if (scanWorkflowWatcher_.isRunning()) {
        return;
    }

    IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    config.calibrationInput = acquisitionPanel_->calibrationInput();
    const auto projectConfig = acquisitionPanel_->projectConfig();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = projectConfig.outputDirectory;
    }

    const ReconstructionInput reconstructionInput = acquisitionPanel_->reconstructionInput(CalibrationResult{});
    config.laserConfig = reconstructionInput.laserConfig;
    config.reconstructionRange = reconstructionInput.imageRange;
    config.matchDistanceThreshold = reconstructionInput.matchDistanceThreshold;
    config.calibrationFile = projectConfig.calibrationFile.empty()
        ? config.calibrationInput.outputFile
        : projectConfig.calibrationFile;

    if (calibration_.isValid() && !config.calibrationFile.empty()) {
        calibrationService_.saveCalibration(config.calibrationFile, calibration_);
    }

    setBusy(true, QString::fromUtf8("扫描重建中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("扫描重建中..."));
    scanWorkflowWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Scan and reconstruct", [this, config]() {
            return integratedScanService_.runScanAndReconstruct(config, [this](int, int, const FramePair& frame) {
                enqueueLivePreview(frame);
            });
        });
    }));
}

void MainWindow::startCalibrationCapture()
{
    if (calibrationCaptureStartWatcher_.isRunning() || calibrationCaptureSessionService_.isActive()) {
        return;
    }

    StereoCameraConfig config = acquisitionPanel_->stereoCameraConfig();
    if (config.outputDirectory.empty()) {
        config.outputDirectory = acquisitionPanel_->projectConfig().outputDirectory;
    }

    setBusy(true, QString::fromUtf8("开始标定采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在连接相机并创建标定采集会话..."));
    calibrationCaptureStartWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Start calibration capture", [this, config]() {
            return calibrationCaptureSessionService_.start(config);
        });
    }));
}

void MainWindow::captureCalibrationFrame()
{
    if (calibrationFrameWatcher_.isRunning() || !calibrationCaptureSessionService_.isActive()) {
        return;
    }

    setBusy(true, QString::fromUtf8("采集当前标定帧..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在采集当前标定帧..."));
    calibrationFrameWatcher_.setFuture(QtConcurrent::run([this]() {
        return runWorkerTask("Capture calibration frame", [this]() {
            return calibrationCaptureSessionService_.captureCurrentFrame();
        });
    }));
}

void MainWindow::calibrateCapturedFrames()
{
    if (calibrationWatcher_.isRunning()) {
        return;
    }

    calibrationCapture_ = calibrationCaptureSessionService_.currentResult();
    if (calibrationCapture_.capturedFrameCount <= 0) {
        QMessageBox::warning(this, QString::fromUtf8("缺少标定帧"), QString::fromUtf8("请先采集至少一组左右标定图像。"));
        return;
    }
    if (calibrationCapture_.capturedFrameCount < 6) {
        const auto reply = QMessageBox::question(
            this,
            QString::fromUtf8("标定帧较少"),
            QString::fromUtf8("当前只采集了 %1 帧，标定精度可能不足。是否继续？").arg(calibrationCapture_.capturedFrameCount));
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    CalibrationInput input = acquisitionPanel_->calibrationInput();
    input.leftDirectory = calibrationCapture_.leftDirectory;
    input.rightDirectory = calibrationCapture_.rightDirectory;
    if (QString::fromStdString(input.outputFile).trimmed().isEmpty()) {
        const QString filePath = defaultCalibrationFilePath();
        input.outputFile = filePath.toStdString();
        acquisitionPanel_->setCalibrationFile(input.outputFile);
    }

    setBusy(true, QString::fromUtf8("标定当前采集帧..."), true);
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在标定当前采集帧..."));
    calibrationWatcher_.setFuture(QtConcurrent::run([input]() {
        return runWorkerTask("Calibration", [input]() {
            CalibrationService service;
            return service.calibrate(input);
        });
    }));
}

/*
    函数功能：将当前内存中的双目标定结果保存到在线标定结果文件夹
    输入：
        无
    输出：
        无（函数会按时间戳生成 yml 文件，并把该文件路径回填为当前标定文件）
*/
void MainWindow::saveCalibrationResult()
{
    if (!calibration_.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("缺少标定结果"), QString::fromUtf8("请先完成一次有效标定，再保存标定结果。"));
        return;
    }

    const QString resultDirectory = calibrationResultsDirectory();
    if (!QDir().mkpath(resultDirectory)) {
        QMessageBox::critical(this, QString::fromUtf8("保存失败"), QString::fromUtf8("无法创建标定结果文件夹：%1").arg(resultDirectory));
        return;
    }

    const QString fileName = "stereo_calibration_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".yml";
    const QString filePath = QDir(resultDirectory).filePath(fileName);

    try {
        calibrationService_.saveCalibration(filePath.toStdString(), calibration_);
        acquisitionPanel_->setCalibrationFile(filePath.toStdString());
        configService_.save(acquisitionPanel_->projectConfig());
        acquisitionPanel_->setStatusText(QString::fromUtf8("标定结果已保存：%1").arg(filePath));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定文件: %1 | RMS: %2").arg(fileName).arg(calibration_.rms));
        refreshProjectTree();
        Logger::instance().info("Calibration", "Timestamped calibration result saved: " + filePath.toStdString());
        QMessageBox::information(this, QString::fromUtf8("保存完成"), QString::fromUtf8("标定结果已保存到：\n%1").arg(filePath));
    } catch (const std::exception& ex) {
        Logger::instance().error("Calibration", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("保存失败"), QString::fromStdString(ex.what()));
    }
}

/*
    函数功能：从在线标定结果文件夹加载历史双目标定文件
    输入：
        无
    输出：
        无（加载成功后会更新当前标定结果和参数面板中的标定文件路径）
*/
void MainWindow::loadCalibrationResult()
{
    const QString resultDirectory = calibrationResultsDirectory();
    QDir().mkpath(resultDirectory);
    const QString file = QFileDialog::getOpenFileName(
        this,
        QString::fromUtf8("加载标定结果"),
        resultDirectory,
        QString::fromUtf8("YAML (*.yml *.yaml);;所有文件 (*.*)"));
    if (file.isEmpty()) {
        return;
    }

    CalibrationResult loaded;
    if (!calibrationService_.loadCalibration(file.toStdString(), loaded)) {
        QMessageBox::warning(this, QString::fromUtf8("加载失败"), QString::fromUtf8("标定文件无效或无法读取。"));
        return;
    }

    calibration_ = loaded;
    acquisitionPanel_->setCalibrationFile(file.toStdString());
    configService_.save(acquisitionPanel_->projectConfig());
    acquisitionPanel_->setStatusText(QString::fromUtf8("已加载标定结果：%1").arg(file));
    acquisitionPanel_->setResultSummary(QString::fromUtf8("标定文件: %1 | RMS: %2").arg(QFileInfo(file).fileName()).arg(calibration_.rms));
    refreshProjectTree();
    Logger::instance().info("Calibration", "Calibration result loaded from result folder: " + file.toStdString());
    QMessageBox::information(this, QString::fromUtf8("加载完成"), QString::fromUtf8("已加载标定结果。RMS: %1").arg(calibration_.rms));
}

void MainWindow::finishCalibrationCapture()
{
    stopLivePreview();
    calibrationCaptureSessionService_.finish();
    calibrationCapture_ = calibrationCaptureSessionService_.currentResult();
    acquisition_ = calibrationCapture_;
    acquisitionPanel_->setCalibrationCaptureState(false, calibrationCapture_.capturedFrameCount);
    acquisitionPanel_->setStatusText(QString::fromUtf8("标定采集已结束，已采集 %1 帧。").arg(calibrationCapture_.capturedFrameCount));
    setCaptureReviewResult(calibrationCapture_);
    startLivePreview();
}

void MainWindow::startReconstructionCapture()
{
    if (reconstructionCaptureWatcher_.isRunning() || reconstructionGalvoPreflightWatcher_.isRunning()) {
        return;
    }
    if (galvoMotionParametersWatcher_.isRunning()) {
        acquisitionPanel_->setStatusText(QString::fromUtf8("振镜运动参数正在下发，请稍后再开始重建采集。"));
        return;
    }

    IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();
    const auto projectConfig = acquisitionPanel_->projectConfig();
    if (config.stereoCamera.outputDirectory.empty()) {
        config.stereoCamera.outputDirectory = projectConfig.outputDirectory;
    }
    if (config.stereoCamera.frameCount <= 0) {
        const QString message = QString::fromUtf8("总旋转角度和步进角度无法得到有效采集帧数，请检查振镜运动参数。");
        acquisitionPanel_->setStatusText(message);
        QMessageBox::warning(this, QString::fromUtf8("无法开始重建采集"), message);
        return;
    }

    pendingReconstructionCaptureConfig_ = config;
    setBusy(true, QString::fromUtf8("正在重新设置并确认振镜参数..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在连接振镜，重新设置步进角度和自动旋转角度，然后回读验证..."));
    acquisitionPanel_->setReconstructionCaptureReady(false);
    reconstructionGalvoPreflightWatcher_.setFuture(QtConcurrent::run([galvoConfig = config.galvo]() {
        return configureAndVerifyGalvoMotionParameters(galvoConfig);
    }));
}

void MainWindow::onReconstructionGalvoPreflightFinished()
{
    const auto result = reconstructionGalvoPreflightWatcher_.result();
    auto config = pendingReconstructionCaptureConfig_;

    acquisitionPanel_->setStatusText(result.message);
    updateGalvoStatusBar(config, result.message);
    if (!result.success) {
        setBusy(false, QString());
        Logger::instance().error("Galvo", result.message.toStdString());
        QMessageBox::warning(this, QString::fromUtf8("重建前振镜检查失败"), result.message);
        pendingReconstructionCaptureConfig_ = {};
        return;
    }

    config.stereoCamera.frameCount = frameCountForGalvoScan(result.actualTotalAngleDeg, result.actualStepAngleDeg);
    if (config.stereoCamera.frameCount <= 0) {
        const QString message = QString::fromUtf8("设备回读的总旋转角度或步进角度无效，无法计算采集帧数。");
        setBusy(false, QString());
        acquisitionPanel_->setStatusText(message);
        Logger::instance().error("Galvo", message.toStdString());
        QMessageBox::warning(this, QString::fromUtf8("重建前振镜检查失败"), message);
        pendingReconstructionCaptureConfig_ = {};
        return;
    }
    config.totalRotationAngleDeg = result.actualTotalAngleDeg;
    config.galvo.autoRotationAngleDeg = result.actualTotalAngleDeg;
    config.galvo.stepAngleDeg = result.actualStepAngleDeg;
    acquisitionPanel_->setFrameCountFromDevice(result.actualStepAngleDeg, result.actualTotalAngleDeg);
    Logger::instance().info("Galvo", (result.message + QString::fromUtf8("，本次采集 %1 帧").arg(config.stereoCamera.frameCount)).toStdString());
    beginReconstructionCapture(config);
    pendingReconstructionCaptureConfig_ = {};
}

void MainWindow::beginReconstructionCapture(const IntegratedScanConfig& config)
{
    statusBar()->showMessage(QString::fromUtf8("重建采集中..."));
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在采集重建图像序列..."));
    acquisitionPanel_->setReconstructionCaptureReady(false);
    reconstructionCaptureWatcher_.setFuture(QtConcurrent::run([this, config]() {
        return runWorkerTask("Reconstruction capture", [this, config]() {
            return reconstructionCaptureSessionService_.capture(config, [this](int, int, const FramePair& frame) {
                enqueueLivePreview(frame);
            });
        });
    }));
}

void MainWindow::reconstructCapturedFrames()
{
    try {
        if (reconstructionWatcher_.isRunning()) {
            return;
        }

        if (reconstructionCapture_.capturedFrameCount <= 0) {
            QMessageBox::warning(this, QString::fromUtf8("缺少重建帧"), QString::fromUtf8("请先执行开始重建采集。"));
            return;
        }

        if (!calibration_.isValid()) {
            const auto file = acquisitionPanel_->projectConfig().calibrationFile;
            calibrationService_.loadCalibration(file, calibration_);
        }
        if (!calibration_.isValid()) {
            QMessageBox::warning(this, QString::fromUtf8("缺少标定"), QString::fromUtf8("请先完成标定或加载有效标定文件。"));
            return;
        }

        ReconstructionInput input = acquisitionPanel_->reconstructionInput(calibration_);
        input.leftDirectory = reconstructionCapture_.leftDirectory;
        input.rightDirectory = reconstructionCapture_.rightDirectory;
        autoExportReconstructionOnFinish_ = true;
        setBusy(true, QString::fromUtf8("重建当前采集帧..."), true);
        acquisitionPanel_->setStatusText(QString::fromUtf8("正在重建当前采集帧..."));
        reconstructionWatcher_.setFuture(QtConcurrent::run(runReconstructionTask, input));
    } catch (const cv::Exception& ex) {
        autoExportReconstructionOnFinish_ = false;
        setBusy(false, QString());
        Logger::instance().error("Reconstruction", std::string("Failed to start captured-frame reconstruction: ") + ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("启动当前采集帧重建时发生 OpenCV 异常：%1").arg(QString::fromStdString(ex.what())));
    } catch (const std::exception& ex) {
        autoExportReconstructionOnFinish_ = false;
        setBusy(false, QString());
        Logger::instance().error("Reconstruction", std::string("Failed to start captured-frame reconstruction: ") + ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromStdString(ex.what()));
    } catch (...) {
        autoExportReconstructionOnFinish_ = false;
        setBusy(false, QString());
        Logger::instance().error("Reconstruction", "Failed to start captured-frame reconstruction with an unknown exception.");
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("启动当前采集帧重建时发生未知异常。"));
    }
}


void MainWindow::sendRawGalvoCommand(const QString& commandText)
{
    if (shuttingDown_.load() || busy_ || galvoMotionParametersWatcher_.isRunning()) {
        return;
    }
    const IntegratedScanConfig config = acquisitionPanel_->integratedScanConfig();

    std::vector<unsigned char> command;
    try {
        command = parseHexCommandText(commandText);
    } catch (const std::exception& ex) {
        serialCommandPackWidget_->setRawGalvoResponseText(QString::fromUtf8("指令格式错误"));
        QMessageBox::warning(this, QString::fromUtf8("相机指令格式错误"), QString::fromStdString(ex.what()));
        return;
    }

    SerialGalvoController controller(config.galvo);
    try {
        serialCommandPackWidget_->setRawGalvoResponseText(QString::fromUtf8("等待返回..."));
        if (!controller.connect()) {
            throw std::runtime_error("Failed to open galvo serial port: " + config.galvo.portName);
        }

        const auto result = controller.sendRawCommand(command, true);
        if (!result.response.empty()) {
            serialCommandPackWidget_->appendRawGalvoResponse(bytesToHexText(result.response));
        }
        if (result.success) {
            serialCommandPackWidget_->setRawGalvoResponseText(QString::fromUtf8("已收到返回，最新指令显示在最上方。"));
        } else if (result.message == "No response received before timeout.") {
            serialCommandPackWidget_->setRawGalvoResponseText(result.response.empty()
                ? QString::fromUtf8("无返回（等待 %1 ms 超时）").arg(config.galvo.commandTimeoutMs)
                : QString::fromUtf8("收到字节但未匹配到有效返回帧；原始字节已列在下方。"));
        } else {
            serialCommandPackWidget_->setRawGalvoResponseText(QString::fromStdString(result.message));
            throw std::runtime_error(result.message.empty() ? "Failed to send raw galvo command." : result.message);
        }

        controller.disconnect();

        if (command.size() >= 4 && command[3] == 0x1A) {
            galvoLaserEnabled_ = true;
        } else if (command.size() >= 4 && command[3] == 0x1B) {
            galvoLaserEnabled_ = false;
        }

        const QString hexText = bytesToHexText(command);
        const QString note = QString::fromUtf8("已发送相机指令：%1").arg(hexText);
        acquisitionPanel_->setStatusText(note);
        updateGalvoStatusBar(config, note);
        Logger::instance().info(
            "Galvo",
            "Raw command sent from UI: " + hexText.toStdString() +
                (result.response.empty() ? ", response=<none>" : ", response=" + bytesToHexText(result.response).toStdString()));

        if (shouldRefreshPreviewAfterRawCommand(command)) {
            const bool shouldResumeLivePreview = livePreviewWatcher_.isRunning() && !busy_;
            if (shouldResumeLivePreview) {
                stopLivePreview();
            }
            try {
                refreshLaserSwitchPreview(config);
            } catch (const std::exception& ex) {
                const std::string message = std::string("Raw command preview refresh failed: ") + ex.what();
                Logger::instance().warning("Acquisition", message);
                acquisitionPanel_->setStatusText(QString::fromStdString(message));
            }
            if (shouldResumeLivePreview && !shuttingDown_.load() && !busy_) {
                startLivePreview();
            }
        }
    } catch (const std::exception& ex) {
        controller.disconnect();
        serialCommandPackWidget_->setRawGalvoResponseText(QString::fromStdString(ex.what()));
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        updateGalvoStatusBar(config, QString::fromStdString(ex.what()));
        Logger::instance().error("Galvo", ex.what());
        QMessageBox::warning(this, QString::fromUtf8("发送相机指令失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::sendSerialCommandPack(const QString& name, const QString& content)
{
    if (shuttingDown_.load() || busy_ || serialCommandPackWatcher_.isRunning() || galvoMotionParametersWatcher_.isRunning()) {
        serialCommandPackWidget_->setStatusText(QString::fromUtf8("串口正在被其他任务使用，请稍后再发送。"));
        return;
    }

    std::vector<SerialCommandPackStep> steps;
    try {
        steps = parseSerialCommandPack(content);
    } catch (const std::exception& ex) {
        serialCommandPackWidget_->setStatusText(QString::fromStdString(ex.what()));
        return;
    }

    const GalvoScanConfig galvoConfig = acquisitionPanel_->integratedScanConfig().galvo;
    const auto stopRequested = serialCommandPackStopRequested_;
    stopRequested->store(false);
    serialCommandPackWidget_->setStatusText(QString::fromUtf8("正在发送 %1，串口 %2；逐条结果见底部日志。")
        .arg(name, QString::fromStdString(galvoConfig.portName)));
    setBusy(true, QString::fromUtf8("串口命令包发送中..."));

    serialCommandPackWatcher_.setFuture(QtConcurrent::run(
        [name, steps = std::move(steps), galvoConfig, stopRequested]() -> SerialCommandPackSendResult {
            SerialCommandPackSendResult summary;
            SerialGalvoController controller(galvoConfig);
            const std::string context = "命令包 [" + name.toStdString() + "]";
            Logger::instance().info("SerialPack", context + " 开始发送，串口=" + galvoConfig.portName);
            if (!controller.connect()) {
                summary.message = QString::fromUtf8("%1：无法打开串口 %2。")
                    .arg(name, QString::fromStdString(galvoConfig.portName));
                Logger::instance().error("SerialPack", summary.message.toStdString());
                return summary;
            }

            summary.success = true;
            for (const auto& step : steps) {
                if (stopRequested->load()) {
                    summary.success = false;
                    summary.message = QString::fromUtf8("%1：窗口关闭，发送已中止。").arg(name);
                    break;
                }
                if (step.type == SerialCommandPackStep::Type::Wait) {
                    Logger::instance().info("SerialPack", context + " 第" + std::to_string(step.lineNumber) +
                        "行：等待 " + std::to_string(step.waitMs) + " ms");
                    int remaining = step.waitMs;
                    while (remaining > 0 && !stopRequested->load()) {
                        const int slice = std::min(remaining, 50);
                        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
                        remaining -= slice;
                    }
                    continue;
                }

                const QString hex = bytesToHexText(step.command);
                const std::string stepContext = context + " 第" + std::to_string(step.lineNumber) +
                    "行：" + hex.toStdString();
                Logger::instance().info("SerialPack", stepContext + " 准备发送" +
                    (step.expectResponse ? "（等待返回）" : "（不等待返回）"));
                const auto result = controller.sendRawCommand(step.command, step.expectResponse);
                if (!result.response.empty()) {
                    summary.receivedCommands.append(bytesToHexText(result.response));
                }
                if (!result.success) {
                    summary.success = false;
                    const QString detail = result.response.empty()
                        ? QString::fromStdString(result.message)
                        : QString::fromUtf8("收到字节但未匹配到有效返回帧：%1")
                            .arg(bytesToHexText(result.response));
                    summary.message = QString::fromUtf8("%1 第 %2 行发送失败：%3")
                        .arg(name).arg(step.lineNumber).arg(detail);
                    Logger::instance().error("SerialPack", stepContext + " 失败：" + result.message);
                    break;
                }

                if (step.command.size() == 5 && step.command[0] == 0x55 && step.command[1] == 0xAA &&
                    step.command[2] == 0x01 && step.command[4] == step.command[3]) {
                    if (step.command[3] == 0x1A || step.command[3] == 0x1B) {
                        summary.laserStateChanged = true;
                        summary.laserEnabled = step.command[3] == 0x1A;
                    }
                }
                Logger::instance().info("SerialPack", stepContext +
                    (step.expectResponse
                        ? " 已收到返回：" + bytesToHexText(result.response).toStdString()
                        : " 串口写入成功；设备动作未确认"));
            }

            if (summary.success && stopRequested->load()) {
                summary.success = false;
                summary.message = QString::fromUtf8("%1：窗口关闭，发送已中止。").arg(name);
            }

            if (!summary.success && summary.laserStateChanged && summary.laserEnabled) {
                const auto offResult = controller.laserOff();
                if (offResult.success) {
                    summary.laserEnabled = false;
                    Logger::instance().warning("SerialPack", context + " 中止后已发送激光关闭命令。");
                } else {
                    summary.message += QString::fromUtf8(" 激光关闭命令发送失败，请检查设备状态。 ");
                    Logger::instance().error("SerialPack", context + " 中止后激光关闭命令发送失败：" + offResult.message);
                }
            }
            controller.disconnect();
            if (summary.success) {
                summary.message = QString::fromUtf8("%1：命令包发送完成；无返回命令只确认串口写入。").arg(name);
                Logger::instance().info("SerialPack", summary.message.toStdString());
            } else {
                Logger::instance().error("SerialPack", summary.message.toStdString());
            }
            return summary;
        }));
}

void MainWindow::onSerialCommandPackFinished()
{
    SerialCommandPackSendResult summary;
    try {
        summary = serialCommandPackWatcher_.result();
    } catch (const std::exception& ex) {
        summary.message = QString::fromStdString(ex.what());
        Logger::instance().error("SerialPack", summary.message.toStdString());
    }
    if (summary.laserStateChanged) {
        galvoLaserEnabled_ = summary.laserEnabled;
    }
    if (shuttingDown_.load()) {
        return;
    }
    for (const QString& response : summary.receivedCommands) {
        serialCommandPackWidget_->appendRawGalvoResponse(response);
    }
    setBusy(false, QString());
    serialCommandPackWidget_->setStatusText(summary.message);
    statusBar()->showMessage(summary.message);
    updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig(), summary.message);
    if (!summary.success) {
        QMessageBox::warning(this, QString::fromUtf8("命令包发送失败"), summary.message);
    }
}

void MainWindow::applyGalvoMotionParameters()
{
    galvoMotionParametersPending_ = true;
    if (shuttingDown_.load() || busy_ || galvoMotionParametersWatcher_.isRunning()) {
        return;
    }

    galvoMotionParametersPending_ = false;
    const GalvoScanConfig galvoConfig = acquisitionPanel_->integratedScanConfig().galvo;
    acquisitionPanel_->setStatusText(QString::fromUtf8("正在向振镜下发运动参数..."));
    updateGalvoStatusBar(acquisitionPanel_->integratedScanConfig(), QString::fromUtf8("正在下发运动参数"));

    galvoMotionParametersWatcher_.setFuture(QtConcurrent::run([galvoConfig]() -> GalvoMotionVerificationResult {
        SerialGalvoController controller(galvoConfig);
        if (!controller.connect()) {
            return { false, QString::fromUtf8("无法打开振镜串口：%1").arg(QString::fromStdString(galvoConfig.portName)) };
        }

        const auto checkSetResult = [](const GalvoCommandResult& result, const char* action) {
            if (!result.success) {
                throw std::runtime_error(std::string("振镜参数设置失败：") + action + ". " + result.message);
            }
        };

        try {
            checkSetResult(controller.setStepAngle(galvoConfig.stepAngleDeg), "步进角度");
            checkSetResult(controller.setAutoRotationAngle(galvoConfig.autoRotationAngleDeg), "总旋转角度");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // Match the reliable manual-query path: reopen the port after the
            // write-only commands so any device-side acknowledgement or
            // parser state cannot affect the following readback.
            const auto reopenController = [&controller, &galvoConfig]() {
                controller.disconnect();
                if (!controller.connect()) {
                    throw std::runtime_error("重新打开振镜串口失败：" + galvoConfig.portName);
                }
            };
            reopenController();

            double actualStepAngle = 0.0;
            GalvoCommandResult stepQueryResult;
            for (int attempt = 0; attempt < 3; ++attempt) {
                stepQueryResult = controller.getStepAngle(actualStepAngle);
                if (stepQueryResult.success) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
            if (!stepQueryResult.success) {
                throw std::runtime_error(
                    std::string("步进角度设置命令已发送，但回读验证失败：") + stepQueryResult.message);
            }

            reopenController();
            int actualAutoRotationAngle = 0;
            GalvoCommandResult autoAngleQueryResult;
            for (int attempt = 0; attempt < 3; ++attempt) {
                autoAngleQueryResult = controller.getAutoRotationAngle(actualAutoRotationAngle);
                if (autoAngleQueryResult.success) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
            if (!autoAngleQueryResult.success) {
                throw std::runtime_error(
                    std::string("总旋转角度设置命令已发送，但回读验证失败：") + autoAngleQueryResult.message);
            }

            constexpr double kStepAngleToleranceDeg = 0.006;
            if (std::abs(actualStepAngle - galvoConfig.stepAngleDeg) > kStepAngleToleranceDeg) {
                throw std::runtime_error(
                    "步进角度回读不一致，设置=" + std::to_string(galvoConfig.stepAngleDeg) +
                    "，实际=" + std::to_string(actualStepAngle));
            }
            if (actualAutoRotationAngle != galvoConfig.autoRotationAngleDeg) {
                throw std::runtime_error(
                    "总旋转角度回读不一致，设置=" + std::to_string(galvoConfig.autoRotationAngleDeg) +
                    "，实际=" + std::to_string(actualAutoRotationAngle));
            }

            controller.disconnect();
            return {
                true,
                QString::fromUtf8("振镜参数已下发并验证：步进 %1°，总角 %2°")
                    .arg(actualStepAngle, 0, 'f', 4)
                    .arg(actualAutoRotationAngle),
                actualStepAngle,
                actualAutoRotationAngle,
                QString::fromStdString(galvoConfig.portName)
            };
        } catch (const std::exception& ex) {
            controller.disconnect();
            return { false, QString::fromUtf8(ex.what()) };
        } catch (...) {
            controller.disconnect();
            return { false, QString::fromUtf8("振镜参数下发失败：未知异常") };
        }
    }));
}

void MainWindow::onGalvoMotionParametersApplied()
{
    const auto result = galvoMotionParametersWatcher_.result();
    const auto config = acquisitionPanel_->integratedScanConfig();
    if (result.success) {
        // 界面在后台下发期间仍可修改，仅在回读匹配当前设置时更新只读帧数。
        if (!galvoMotionParametersPending_ &&
            result.portName == QString::fromStdString(config.galvo.portName) &&
            std::abs(result.actualStepAngleDeg - config.galvo.stepAngleDeg) <= 0.006 &&
            result.actualTotalAngleDeg == config.galvo.autoRotationAngleDeg) {
            acquisitionPanel_->setFrameCountFromDevice(result.actualStepAngleDeg, result.actualTotalAngleDeg);
        }
        acquisitionPanel_->setStatusText(result.message);
        updateGalvoStatusBar(config, result.message);
        Logger::instance().info("Galvo", result.message.toStdString());
    } else {
        acquisitionPanel_->setStatusText(result.message);
        updateGalvoStatusBar(config, result.message);
        Logger::instance().error("Galvo", result.message.toStdString());
    }

    if (galvoMotionParametersPending_ && !shuttingDown_.load() && !busy_) {
        applyGalvoMotionParameters();
    }
}

void MainWindow::updateGalvoStatusBar(const IntegratedScanConfig& config, const QString& note)
{
    if (!galvoStatusLabel_) {
        return;
    }

    const QString laserState = galvoLaserEnabled_ ? QString::fromUtf8("开") : QString::fromUtf8("关");
    QString text = QString::fromUtf8("振镜 %1 | %2 | %3 | 步进 %4° | 总角 %5° | 自动 %6° | 抓图 %7ms | 等待 %8ms | 占空比 %9 | 电压 %10V | 激光 %11")
        .arg(QString::fromStdString(config.galvo.portName))
        .arg(syncModeText(config.galvo.syncMode))
        .arg(directionText(config.galvo.direction))
        .arg(config.galvo.stepAngleDeg, 0, 'f', 4)
        .arg(config.totalRotationAngleDeg, 0, 'f', 2)
        .arg(config.galvo.autoRotationAngleDeg)
        .arg(config.galvo.captureIntervalMs)
        .arg(config.galvo.continuousCaptureWaitMs)
        .arg(config.galvo.laserDuty)
        .arg(config.galvo.voltageRangeV, 0, 'f', 1)
        .arg(laserState);

    if (!note.isEmpty()) {
        text += QString::fromUtf8(" | ") + note;
    }
    galvoStatusLabel_->setText(text);
}

void MainWindow::onCalibrationFinished()
{
    setBusy(false, QString());
    try {
        calibration_ = calibrationWatcher_.result();
        if (!calibrationCapture_.leftDirectory.empty() && !calibrationCapture_.rightDirectory.empty()) {
            acquisitionPanel_->setCalibrationDirectories(calibrationCapture_.leftDirectory, calibrationCapture_.rightDirectory);
            acquisitionPanel_->setResultSummary(QString::fromUtf8("RMS: %1 | 标定帧: %2")
                .arg(calibration_.rms)
                .arg(calibrationCapture_.capturedFrameCount));
            refreshProjectTree();
        }
        QMessageBox::information(this, QString::fromUtf8("标定完成"), QString::fromUtf8("双目标定完成。RMS: %1").arg(calibration_.rms));
    } catch (const std::exception& ex) {
        Logger::instance().error("Calibration", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("标定失败"), QString::fromStdString(ex.what()));
    }
}

/*
    函数功能：处理后台重建任务完成后的 UI 更新
    输入：
        无
    输出：
        无（函数会刷新点云视图和任务状态）
*/
void MainWindow::onReconstructionFinished()
{
    setBusy(false, QString());
    try {
        reconstruction_ = reconstructionWatcher_.result();
        if (!reconstruction_.success || reconstruction_.mergedPoints.empty()) {
            autoExportReconstructionOnFinish_ = false;
            pointCloudView_->clear();
            const QString message = reconstruction_.message.empty()
                ? QString::fromUtf8("没有重建出有效点云，请检查标定文件、ROI、激光阈值和采集图像。")
                : QString::fromStdString(reconstruction_.message);
            Logger::instance().warning("Reconstruction", message.toStdString());
            QMessageBox::warning(this, QString::fromUtf8("重建失败"), message);
            return;
        }
        if (autoExportReconstructionOnFinish_ && !reconstruction_.mergedPoints.empty()) {
            const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
            reconstruction_.txtPath = outputPath("point_cloud_" + timestamp + ".txt").toStdString();
            reconstruction_.pcdPath = outputPath("point_cloud_" + timestamp + ".pcd").toStdString();
            pointCloudService_.saveTxt(reconstruction_.txtPath, reconstruction_.mergedPoints);
            pointCloudService_.savePcd(reconstruction_.pcdPath, reconstruction_.mergedPoints);
            autoExportReconstructionOnFinish_ = false;
        }
        pointCloudView_->setPoints(reconstruction_.mergedPoints);
        refreshProjectTree();
        QMessageBox::information(this, QString::fromUtf8("重建完成"), QString::fromUtf8("点云数量: %1").arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size())));
    } catch (const WorkerException& ex) {
        autoExportReconstructionOnFinish_ = false;
        Logger::instance().error("Reconstruction", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromStdString(ex.what()));
    } catch (const cv::Exception& ex) {
        autoExportReconstructionOnFinish_ = false;
        Logger::instance().error("Reconstruction", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("OpenCV 重建异常：%1").arg(QString::fromStdString(ex.what())));
    } catch (const std::exception& ex) {
        autoExportReconstructionOnFinish_ = false;
        Logger::instance().error("Reconstruction", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromStdString(ex.what()));
    } catch (...) {
        autoExportReconstructionOnFinish_ = false;
        Logger::instance().error("Reconstruction", "Reconstruction failed with an unknown exception.");
        QMessageBox::critical(this, QString::fromUtf8("重建失败"), QString::fromUtf8("重建时发生未知异常。"));
    }
}

/*
    函数功能：处理后台在线采集任务完成后的 UI 更新
    输入：
        无
    输出：
        无（函数会更新预览图、采集状态，并在成功时把输出目录回填到重建输入）
*/
void MainWindow::onAcquisitionFinished()
{
    setBusy(false, QString());
    try {
        acquisition_ = acquisitionWatcher_.result();
        acquisitionPanel_->setStatusText(QString::fromStdString(acquisition_.message));
        setCaptureReviewResult(acquisition_);
        setLiveStereoImages(acquisition_.lastLeftPreview, acquisition_.lastRightPreview);
        if (acquisition_.success) {
            acquisitionPanel_->setReconstructionDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
            refreshProjectTree();
            QMessageBox::information(this, QString::fromUtf8("采集完成"), QString::fromUtf8("采集完成，成功帧数: %1").arg(acquisition_.capturedFrameCount));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("采集无有效帧"), QString::fromStdString(acquisition_.message));
        }
    } catch (const std::exception& ex) {
        Logger::instance().error("Acquisition", ex.what());
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        QMessageBox::critical(this, QString::fromUtf8("采集失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onCalibrationCaptureStarted()
{
    setBusy(false, QString());
    try {
        const auto state = calibrationCaptureStartWatcher_.result();
        calibrationCapture_ = state.acquisition;
        acquisition_ = calibrationCapture_;
        acquisitionPanel_->setCalibrationCaptureState(state.active, calibrationCapture_.capturedFrameCount);
        acquisitionPanel_->setCalibrationDirectories(calibrationCapture_.leftDirectory, calibrationCapture_.rightDirectory);
        acquisitionPanel_->setCalibrationFile(defaultCalibrationFilePath().toStdString());
        setCaptureReviewResult(calibrationCapture_);
        acquisitionPanel_->setStatusText(QString::fromStdString(calibrationCapture_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定采集帧: %1").arg(calibrationCapture_.capturedFrameCount));
        refreshProjectTree();
    } catch (const std::exception& ex) {
        calibrationCaptureSessionService_.finish();
        acquisitionPanel_->setCalibrationCaptureState(false, 0);
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        Logger::instance().error("CalibrationCapture", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("开始标定采集失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onCalibrationFrameCaptured()
{
    setBusy(false, QString());
    try {
        calibrationCapture_ = calibrationFrameWatcher_.result();
        acquisition_ = calibrationCapture_;
        acquisitionPanel_->setCalibrationCaptureState(calibrationCaptureSessionService_.isActive(), calibrationCapture_.capturedFrameCount);
        acquisitionPanel_->setStatusText(QString::fromStdString(calibrationCapture_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定采集帧: %1").arg(calibrationCapture_.capturedFrameCount));
        setCaptureReviewResult(calibrationCapture_);
        setLiveStereoImages(calibrationCapture_.lastLeftPreview, calibrationCapture_.lastRightPreview);
        acquisitionPanel_->setCalibrationDirectories(calibrationCapture_.leftDirectory, calibrationCapture_.rightDirectory);
        refreshProjectTree();
    } catch (const std::exception& ex) {
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        Logger::instance().error("CalibrationCapture", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("采集当前帧失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onReconstructionCaptureFinished()
{
    setBusy(false, QString());
    try {
        reconstructionCapture_ = reconstructionCaptureWatcher_.result();
        acquisition_ = reconstructionCapture_;
        acquisitionPanel_->setStatusText(QString::fromStdString(reconstructionCapture_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("重建采集帧: %1").arg(reconstructionCapture_.capturedFrameCount));
        acquisitionPanel_->setReconstructionCaptureReady(reconstructionCapture_.success);
        setCaptureReviewResult(reconstructionCapture_);
        setLiveStereoImages(reconstructionCapture_.lastLeftPreview, reconstructionCapture_.lastRightPreview);
        if (reconstructionCapture_.success) {
            acquisitionPanel_->setReconstructionDirectories(reconstructionCapture_.leftDirectory, reconstructionCapture_.rightDirectory);
            refreshProjectTree();
            QMessageBox::information(
                this,
                QString::fromUtf8("重建采集完成"),
                QString::fromUtf8("重建采集完成，成功帧数: %1").arg(reconstructionCapture_.capturedFrameCount));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("重建采集无有效帧"), QString::fromStdString(reconstructionCapture_.message));
        }
    } catch (const std::exception& ex) {
        acquisitionPanel_->setReconstructionCaptureReady(false);
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        Logger::instance().error("ReconstructionCapture", ex.what());
        QMessageBox::critical(this, QString::fromUtf8("重建采集失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onAutoCalibrationFinished()
{
    setBusy(false, QString());
    try {
        autoCalibrationWorkflow_ = autoCalibrationWatcher_.result();
        acquisition_ = autoCalibrationWorkflow_.acquisition;
        acquisitionPanel_->setStatusText(QString::fromStdString(autoCalibrationWorkflow_.message));
        acquisitionPanel_->setResultSummary(QString::fromUtf8("RMS: %1").arg(autoCalibrationWorkflow_.calibration.rms));
        setCaptureReviewResult(acquisition_);
        setLiveStereoImages(acquisition_.lastLeftPreview, acquisition_.lastRightPreview);
        if (autoCalibrationWorkflow_.success) {
            calibration_ = autoCalibrationWorkflow_.calibration;
            acquisitionPanel_->setCalibrationDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
            refreshProjectTree();
            QMessageBox::information(
                this,
                QString::fromUtf8("自动标定完成"),
                QString::fromUtf8("标定完成，RMS: %1").arg(calibration_.rms));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("自动标定失败"), QString::fromStdString(autoCalibrationWorkflow_.message));
        }
    } catch (const std::exception& ex) {
        Logger::instance().error("IntegratedWorkflow", ex.what());
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        QMessageBox::critical(this, QString::fromUtf8("自动标定失败"), QString::fromStdString(ex.what()));
    }
}

void MainWindow::onScanAndReconstructFinished()
{
    setBusy(false, QString());
    try {
        scanWorkflow_ = scanWorkflowWatcher_.result();
        acquisition_ = scanWorkflow_.acquisition;
        acquisitionPanel_->setStatusText(QString::fromStdString(scanWorkflow_.message));
        acquisitionPanel_->setResultSummary(
            QString::fromUtf8("点云: %1 | 复用标定: %2")
                .arg(static_cast<qulonglong>(scanWorkflow_.reconstruction.mergedPoints.size()))
                .arg(scanWorkflow_.usedExistingCalibration ? QString::fromUtf8("是") : QString::fromUtf8("否")));
        setCaptureReviewResult(acquisition_);
        setLiveStereoImages(acquisition_.lastLeftPreview, acquisition_.lastRightPreview);
        if (scanWorkflow_.success) {
            reconstruction_ = scanWorkflow_.reconstruction;
            calibration_ = scanWorkflow_.calibration;
            pointCloudView_->setPoints(reconstruction_.mergedPoints);
            acquisitionPanel_->setReconstructionDirectories(acquisition_.leftDirectory, acquisition_.rightDirectory);
            refreshProjectTree();
            QMessageBox::information(
                this,
                QString::fromUtf8("扫描重建完成"),
                QString::fromUtf8("点云数量: %1").arg(static_cast<qulonglong>(reconstruction_.mergedPoints.size())));
        } else {
            QMessageBox::warning(this, QString::fromUtf8("扫描重建失败"), QString::fromStdString(scanWorkflow_.message));
        }
    } catch (const std::exception& ex) {
        Logger::instance().error("IntegratedWorkflow", ex.what());
        acquisitionPanel_->setStatusText(QString::fromStdString(ex.what()));
        QMessageBox::critical(this, QString::fromUtf8("扫描重建失败"), QString::fromStdString(ex.what()));
    }
}

// 顶部菜单栏负责暴露保存、采集、标定、重建和导出等主流程入口。
void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件"));
    fileMenu->addAction(QString::fromUtf8("保存项目"), this, &MainWindow::saveProjectSettings);
    fileMenu->addAction(QString::fromUtf8("加载标定"), this, &MainWindow::loadCalibration);
    fileMenu->addSeparator();
    fileMenu->addAction(QString::fromUtf8("退出"), qApp, &QApplication::quit);

    auto* scanMenu = menuBar()->addMenu(QString::fromUtf8("扫描"));
    scanMenu->addAction(QString::fromUtf8("刷新相机"), this, &MainWindow::refreshAcquisitionDevices);
    scanMenu->addSeparator();
    scanMenu->addAction(QString::fromUtf8("离线双目标定"), this, &MainWindow::runCalibration);
    scanMenu->addAction(QString::fromUtf8("离线三维重建"), this, &MainWindow::runReconstruction);

    auto* viewMenu = menuBar()->addMenu(QString::fromUtf8("显示"));
    viewMenu->addAction(QString::fromUtf8("清空点云"), pointCloudView_, &PointCloudViewWidget::clear);

    menuBar()->addMenu(QString::fromUtf8("设置"));
    menuBar()->addMenu(QString::fromUtf8("帮助"));
}

// 工具栏提供最常用的一组快捷操作，便于离线处理和在线采集快速切换。
void MainWindow::buildToolBar()
{
    auto* toolbar = addToolBar(QString::fromUtf8("工具"));
    toolbar->setMovable(false);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QString::fromUtf8("保存"), this, &MainWindow::saveProjectSettings);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), QString::fromUtf8("加载标定"), this, &MainWindow::loadCalibration);
    toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), QString::fromUtf8("刷新相机"), this, &MainWindow::refreshAcquisitionDevices);
    toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QString::fromUtf8("离线标定"), this, &MainWindow::runCalibration);
    toolbar->addAction(style()->standardIcon(QStyle::SP_ComputerIcon), QString::fromUtf8("离线重建"), this, &MainWindow::runReconstruction);
    toolbar->addSeparator();
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QString::fromUtf8("导出TXT"), this, &MainWindow::exportTxt);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DriveHDIcon), QString::fromUtf8("导出PCD"), this, &MainWindow::exportPcd);
}

// Dock 区域保留右侧在线工作流和底部日志面板，主显示区留给图像与点云。
void MainWindow::buildDocks()
{
    acquisitionPanel_ = new AcquisitionPanel;
    serialCommandPackWidget_->setRawGalvoCommandWidget(acquisitionPanel_->rawGalvoCommandWidget());
    auto* acquisitionDock = new QDockWidget(QString::fromUtf8("在线工作流"), this);
    acquisitionDock->setWidget(acquisitionPanel_);
    addDockWidget(Qt::RightDockWidgetArea, acquisitionDock);

    logPanel_ = new LogPanel;
    auto* bottomDock = new QDockWidget(QString::fromUtf8("消息"), this);
    bottomDock->setWidget(logPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
}

// 中央区域以标签页形式承载双目实时预览、采集筛选和点云视图。
void MainWindow::buildCentralView()
{
    auto* tabs = new QTabWidget;
    auto* liveStereoPage = new QWidget;
    auto* liveStereoStackLayout = new QGridLayout(liveStereoPage);
    liveStereoStackLayout->setContentsMargins(0, 0, 0, 0);
    liveStereoStackLayout->setSpacing(0);

    auto* liveStereoContent = new QWidget;
    auto* liveStereoLayout = new QHBoxLayout(liveStereoContent);
    liveStereoLayout->setContentsMargins(0, 0, 0, 0);
    liveStereoLayout->setSpacing(0);

    auto* leftLivePane = new QWidget;
    auto* leftLiveLayout = new QVBoxLayout(leftLivePane);
    leftLiveLayout->setContentsMargins(0, 0, 0, 0);
    liveLeftImageView_ = new ImageViewWidget;
    leftLiveLayout->addWidget(liveLeftImageView_, 1);

    auto* rightLivePane = new QWidget;
    auto* rightLiveLayout = new QVBoxLayout(rightLivePane);
    rightLiveLayout->setContentsMargins(0, 0, 0, 0);
    liveRightImageView_ = new ImageViewWidget;
    rightLiveLayout->addWidget(liveRightImageView_, 1);

    auto* stereoDivider = new QFrame;
    stereoDivider->setFixedWidth(2);
    stereoDivider->setFrameShape(QFrame::NoFrame);
    stereoDivider->setStyleSheet("background: #000000;");

    liveStereoLayout->addWidget(leftLivePane, 1);
    liveStereoLayout->addWidget(stereoDivider);
    liveStereoLayout->addWidget(rightLivePane, 1);

    liveFpsLabel_ = new QLabel;
    liveFpsLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    liveFpsLabel_->setStyleSheet(
        "QLabel { background: rgba(0, 0, 0, 150); color: #ffffff; padding: 4px 8px; border-radius: 3px; }");
    liveStereoStackLayout->addWidget(liveStereoContent, 0, 0);
    liveStereoStackLayout->addWidget(liveFpsLabel_, 0, 0, Qt::AlignLeft | Qt::AlignBottom);
    liveFpsLabel_->raise();
    resetLiveFps();

    captureReviewWidget_ = new CaptureReviewWidget;
    connect(captureReviewWidget_, &CaptureReviewWidget::captureResultChanged, this, &MainWindow::onCaptureReviewResultChanged);
    serialCommandPackWidget_ = new SerialCommandPackWidget;

    pointCloudView_ = new PointCloudViewWidget;
    tabs->addTab(liveStereoPage, QString::fromUtf8("双目"));
    tabs->addTab(pointCloudView_, QString::fromUtf8("点云"));
    tabs->addTab(captureReviewWidget_, QString::fromUtf8("采集"));
    tabs->addTab(serialCommandPackWidget_, QString::fromUtf8("串口"));
    setCentralWidget(tabs);
}

void MainWindow::refreshProjectTree()
{
}

// 忙碌状态统一驱动底部进度条和在线采集面板按钮的可用性。
void MainWindow::setBusy(bool busy, const QString& text, bool keepLivePreview)
{
    busy_ = busy;
    if (busy_ && !keepLivePreview) {
        stopLivePreview();
    }

    progressBar_->setRange(busy ? 0 : 0, busy ? 0 : 100);
    progressBar_->setValue(busy ? 0 : 100);
    statusBar()->showMessage(text);
    if (acquisitionPanel_) {
        acquisitionPanel_->setBusy(busy);
    }
    if (captureReviewWidget_) {
        captureReviewWidget_->setEnabled(!busy);
    }
    if (serialCommandPackWidget_) {
        serialCommandPackWidget_->setSendingAvailable(!busy);
    }

    if (!busy_) {
        if (shuttingDown_.load()) {
            return;
        }
        startLivePreview();
    }
}

void MainWindow::enqueueLivePreview(const FramePair& frame)
{
    if (shuttingDown_.load()) {
        return;
    }

    cv::Mat left = frame.left.clone();
    cv::Mat right = frame.right.clone();
    QMetaObject::invokeMethod(
        this,
        [this, left = std::move(left), right = std::move(right)]() mutable {
            if (shuttingDown_.load()) {
                return;
            }
            setLiveStereoImages(left, right);
        },
        Qt::QueuedConnection);
}

void MainWindow::setLiveStereoImages(const cv::Mat& leftImage, const cv::Mat& rightImage)
{
    if (!leftImage.empty() && liveLeftImageView_) {
        liveLeftImageView_->setImage(leftImage);
    }
    if (!rightImage.empty() && liveRightImageView_) {
        liveRightImageView_->setImage(rightImage);
    }
    if (!leftImage.empty() || !rightImage.empty()) {
        updateLiveFps();
    }
}

void MainWindow::updateLiveFps()
{
    if (!liveFpsLabel_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (liveFpsWindowStart_.time_since_epoch().count() == 0) {
        liveFpsWindowStart_ = now;
        liveFpsFrameCount_ = 0;
    }

    ++liveFpsFrameCount_;
    const double elapsedSeconds = std::chrono::duration<double>(now - liveFpsWindowStart_).count();
    if (elapsedSeconds < 1.0) {
        return;
    }

    const double fps = static_cast<double>(liveFpsFrameCount_) / elapsedSeconds;
    liveFpsLabel_->setText(QString::fromUtf8("帧率: %1 FPS").arg(fps, 0, 'f', 1));
    liveFpsWindowStart_ = now;
    liveFpsFrameCount_ = 0;
}

void MainWindow::resetLiveFps()
{
    liveFpsWindowStart_ = std::chrono::steady_clock::now();
    liveFpsFrameCount_ = 0;
    if (liveFpsLabel_) {
        liveFpsLabel_->setText(QString::fromUtf8("帧率: -- FPS"));
    }
}

void MainWindow::setCaptureReviewResult(const AcquisitionSessionResult& result)
{
    if (captureReviewWidget_) {
        captureReviewWidget_->setCaptureResult(result);
    }
}

void MainWindow::onCaptureReviewResultChanged()
{
    if (!captureReviewWidget_) {
        return;
    }

    const AcquisitionSessionResult result = captureReviewWidget_->captureResult();
    acquisition_ = result;

    if (!calibrationCapture_.sessionDirectory.empty() && result.sessionDirectory == calibrationCapture_.sessionDirectory) {
        calibrationCapture_ = result;
        calibrationCaptureSessionService_.setCurrentResult(result);
        acquisitionPanel_->setCalibrationCaptureState(calibrationCaptureSessionService_.isActive(), calibrationCapture_.capturedFrameCount);
        acquisitionPanel_->setResultSummary(QString::fromUtf8("标定采集帧: %1").arg(calibrationCapture_.capturedFrameCount));
    }

    if (!reconstructionCapture_.sessionDirectory.empty() && result.sessionDirectory == reconstructionCapture_.sessionDirectory) {
        reconstructionCapture_ = result;
        acquisitionPanel_->setReconstructionCaptureReady(reconstructionCapture_.capturedFrameCount > 0);
        acquisitionPanel_->setResultSummary(QString::fromUtf8("重建采集帧: %1").arg(reconstructionCapture_.capturedFrameCount));
    }

    if (!autoCalibrationWorkflow_.acquisition.sessionDirectory.empty() &&
        result.sessionDirectory == autoCalibrationWorkflow_.acquisition.sessionDirectory) {
        autoCalibrationWorkflow_.acquisition = result;
    }

    if (!scanWorkflow_.acquisition.sessionDirectory.empty() &&
        result.sessionDirectory == scanWorkflow_.acquisition.sessionDirectory) {
        scanWorkflow_.acquisition = result;
    }

    acquisitionPanel_->setStatusText(QString::fromUtf8("已删除采集帧，当前剩余 %1 帧。").arg(result.capturedFrameCount));
}


void MainWindow::startLivePreview()
{
    if (shuttingDown_.load() || !acquisitionPanel_ || busy_ || livePreviewWatcher_.isRunning()) {
        return;
    }

    if (calibrationCaptureSessionService_.isActive()) {
        resetLiveFps();
        livePreviewStopRequested_.store(false);
        livePreviewWatcher_.setFuture(QtConcurrent::run([this]() {
            runCalibrationCapturePreviewLoop();
        }));
        return;
    }

    StereoCameraConfig config = acquisitionPanel_->stereoCameraConfig();
    config.frameCount = std::numeric_limits<int>::max();
    config.leftParameters.useHardwareTrigger = false;
    config.rightParameters.useHardwareTrigger = false;

    if (!config.useMockProvider) {
        if (config.leftDeviceId.empty() || config.rightDeviceId.empty()) {
            return;
        }
        if (config.leftDeviceId == config.rightDeviceId) {
            acquisitionPanel_->setStatusText(QString::fromUtf8("实时预览需要选择两台不同的相机。"));
            return;
        }
    }

    resetLiveFps();
    livePreviewStopRequested_.store(false);
    livePreviewWatcher_.setFuture(QtConcurrent::run([this, config]() {
        runLivePreviewLoop(config);
    }));
}

void MainWindow::stopLivePreview()
{
    livePreviewStopRequested_.store(true);
    if (livePreviewWatcher_.isRunning()) {
        livePreviewWatcher_.waitForFinished();
    }
}

void MainWindow::restartLivePreview()
{
    if (busy_) {
        return;
    }

    stopLivePreview();
    startLivePreview();
}

void MainWindow::runLivePreviewLoop(StereoCameraConfig config)
{
    try {
        AcquisitionService service;
        auto provider = service.createProvider(config);
        Logger::instance().info("Acquisition", "Live stereo preview started.");

        while (!shuttingDown_.load() && !livePreviewStopRequested_.load() && provider->hasNext()) {
            FramePair frame = provider->next();
            if (!frame.left.empty() || !frame.right.empty()) {
                enqueueLivePreview(frame);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }

        Logger::instance().info("Acquisition", "Live stereo preview stopped.");
    } catch (const std::exception& ex) {
        if (!shuttingDown_.load() && !livePreviewStopRequested_.load()) {
            Logger::instance().warning("Acquisition", std::string("Live stereo preview stopped: ") + ex.what());
            QMetaObject::invokeMethod(
                this,
                [this, message = QString::fromStdString(ex.what())]() {
                    if (shuttingDown_.load()) {
                        return;
                    }
                    if (acquisitionPanel_ && !busy_) {
                        acquisitionPanel_->setStatusText(QString::fromUtf8("实时预览未启动：%1").arg(message));
                    }
                },
                Qt::QueuedConnection);
        }
    }
}

void MainWindow::runCalibrationCapturePreviewLoop()
{
    Logger::instance().info("Acquisition", "Calibration capture live preview started.");
    while (!shuttingDown_.load() && !livePreviewStopRequested_.load() && calibrationCaptureSessionService_.isActive()) {
        FramePair frame = calibrationCaptureSessionService_.grabPreviewFrame();
        if (!frame.left.empty() || !frame.right.empty()) {
            enqueueLivePreview(frame);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    Logger::instance().info("Acquisition", "Calibration capture live preview stopped.");
}
void MainWindow::refreshLaserSwitchPreview(const IntegratedScanConfig& config)
{
    if (config.stereoCamera.useMockProvider) {
        Logger::instance().warning("Acquisition", "Laser switch preview refresh skipped because mock acquisition is enabled.");
        return;
    }

#if HTMSR_WITH_HIK_CAMERA
    auto cameraConfig = config.stereoCamera;
    cameraConfig.leftParameters.useHardwareTrigger = false;
    cameraConfig.rightParameters.useHardwareTrigger = false;

    const auto devices = enumerateHikCameraDevices();
    HikCameraDevice leftCamera(findSelectedCameraDevice(devices, cameraConfig.leftDeviceId));
    HikCameraDevice rightCamera(findSelectedCameraDevice(devices, cameraConfig.rightDeviceId));

    try {
        if (!leftCamera.connect() || !rightCamera.connect()) {
            throw std::runtime_error("Failed to connect selected Hik cameras for preview refresh.");
        }
        if (!leftCamera.configure(cameraConfig.leftParameters) || !rightCamera.configure(cameraConfig.rightParameters)) {
            throw std::runtime_error("Failed to configure selected Hik cameras for preview refresh.");
        }
        if (!leftCamera.startGrabbing() || !rightCamera.startGrabbing()) {
            throw std::runtime_error("Failed to start selected Hik cameras for preview refresh.");
        }

        FramePair frame;
        frame.left = leftCamera.grabFrame(cameraConfig.leftParameters.grabTimeoutMs);
        frame.right = rightCamera.grabFrame(cameraConfig.rightParameters.grabTimeoutMs);

        leftCamera.stopGrabbing();
        rightCamera.stopGrabbing();
        leftCamera.disconnect();
        rightCamera.disconnect();

        if (frame.left.empty() || frame.right.empty()) {
            throw std::runtime_error("Preview refresh captured an empty camera frame.");
        }

        LaserExtractionService extractor;
        const auto extraction = extractor.extract(frame.left, config.laserConfig.leftRoi, config.laserConfig);
        cv::Mat left = frame.left.clone();
        cv::Mat right = frame.right.clone();
        if (shuttingDown_.load()) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, left = std::move(left), right = std::move(right)]() mutable {
                if (shuttingDown_.load()) {
                    return;
                }
                setLiveStereoImages(left, right);
            },
            Qt::QueuedConnection);
        Logger::instance().info(
            "Acquisition",
            "Laser switch preview frame refreshed, extracted left laser points=" + std::to_string(extraction.points.size()));
    } catch (...) {
        leftCamera.stopGrabbing();
        rightCamera.stopGrabbing();
        leftCamera.disconnect();
        rightCamera.disconnect();
        throw;
    }
#else
    (void)config;
    Logger::instance().warning("Acquisition", "Laser switch preview refresh skipped because Hik camera support is disabled.");
#endif
}

QString MainWindow::calibrationResultsDirectory() const
{
    StereoCameraConfig cameraConfig;
    if (acquisitionPanel_) {
        cameraConfig = acquisitionPanel_->stereoCameraConfig();
    }

    QString root = QString::fromStdString(cameraConfig.outputDirectory);
    if (root.trimmed().isEmpty()) {
        root = QString::fromStdString(acquisitionPanel_->projectConfig().outputDirectory);
    }
    if (root.trimmed().isEmpty()) {
        root = ".";
    }

    return QDir(root).filePath("calibration/result");
}

QString MainWindow::defaultCalibrationFilePath() const
{
    const QString resultDirectory = calibrationResultsDirectory();
    QDir().mkpath(resultDirectory);
    const QString fileName = "stereo_calibration_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".yml";
    return QDir(resultDirectory).filePath(fileName);
}

// 导出路径默认基于当前项目输出目录拼接，减少每次导出时重复选路径。
QString MainWindow::outputPath(const QString& filename) const
{
    const auto config = acquisitionPanel_->projectConfig();
    QString root = QString::fromStdString(config.outputDirectory);
    if (root.trimmed().isEmpty()) {
        root = ".";
    }

    const QString resultDirectory = QDir(root).filePath("reconstruction");
    QDir().mkpath(resultDirectory);
    return QDir(resultDirectory).filePath(filename);
}

} // namespace htmsr::app
