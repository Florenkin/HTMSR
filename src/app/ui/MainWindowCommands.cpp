#include "app/ui/MainWindowTasks.h"

namespace htmsr::app {
using namespace detail;

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
            throw std::runtime_error(controller.lastError());
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
                summary.message = QString::fromUtf8("%1：%2")
                    .arg(name, QString::fromStdString(controller.lastError()));
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
                    Logger::instance().debug("SerialPack", context + " 第" + std::to_string(step.lineNumber) +
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
                Logger::instance().debug("SerialPack", stepContext + " 准备发送" +
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
                Logger::instance().debug("SerialPack", stepContext +
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
    if (shuttingDown_.load()) return;
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
            return { false, QString::fromStdString(controller.lastError()) };
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
            const auto reopenController = [&controller]() {
                controller.disconnect();
                if (!controller.connect()) {
                    throw std::runtime_error(controller.lastError());
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
    if (shuttingDown_.load()) return;
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
    const bool cameraUsesHardwareTrigger = config.stereoCamera.leftParameters.useHardwareTrigger ||
        config.stereoCamera.rightParameters.useHardwareTrigger;
    const QString cameraMode = cameraUsesHardwareTrigger ? QString::fromUtf8("相机硬触发") : QString::fromUtf8("相机软件抓图");
    QString text = QString::fromUtf8("振镜 %1 | %2 | %3 | 步进 %4° | 总角 %5° | 自动 %6° | 抓图 %7ms | 等待 %8ms | 占空比 %9 | 电压 %10V | 激光 %11")
        .arg(QString::fromStdString(config.galvo.portName))
        .arg(syncModeText(config.galvo.syncMode) + QStringLiteral(" / ") + cameraMode)
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

} // namespace htmsr::app
