#pragma once

#include "app/acquisition/GalvoController.h"
#include "app/acquisition/GalvoCaptureSupport.h"
#include "app/services/ReconstructionStorage.h"
#include "app/services/LaserExtractionStorage.h"
#include "app/services/ConfigAutoSave.h"
#include "app/services/ResultExportService.h"
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
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
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

#include "app/ui/MainWindow.h"
namespace htmsr::app::detail {

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

inline htmsr::ReconstructionResult failedReconstructionResult(const std::string& message)
{
    htmsr::ReconstructionResult result;
    result.success = false;
    result.message = message;
    return result;
}

inline ReconstructionTaskResult runReconstructionTask(const htmsr::ReconstructionInput& input,
    const std::string& outputDirectory, const std::string& captureSessionDirectory,
    const std::string& laserExtractionDirectory, bool saveLaserExtractionImages,
    htmsr::CancellationToken cancellation)
{
    ReconstructionTaskResult taskResult;
    std::unique_ptr<LaserExtractionStorage> laserStorage;
    if (saveLaserExtractionImages) {
        laserStorage = std::make_unique<LaserExtractionStorage>(laserExtractionDirectory);
    }

    const auto captureLaserResult = [&]() {
        if (laserStorage) {
            taskResult.laserExtractionImages = laserStorage->result();
        }
    };

    try {
        htmsr::ReconstructionService service;
        htmsr::ReconstructionService::FramePreviewCallback previewCallback;
        if (laserStorage && laserStorage->isReady()) {
            previewCallback = [&laserStorage](int frameIndex, const cv::Mat& leftPreview, const cv::Mat& rightPreview) {
                laserStorage->savePair(frameIndex, leftPreview, rightPreview);
            };
        }
        taskResult.reconstruction = service.reconstruct(input, cancellation, previewCallback);
        captureLaserResult();
        cancellation.check();
        htmsr::app::ReconstructionStorage::savePointClouds(
            outputDirectory, taskResult.reconstruction, captureSessionDirectory);
    } catch (const cv::Exception& ex) {
        captureLaserResult();
        taskResult.reconstruction = failedReconstructionResult(std::string("OpenCV 重建异常：") + ex.what());
    } catch (const std::exception& ex) {
        captureLaserResult();
        taskResult.reconstruction = failedReconstructionResult(ex.what());
    } catch (...) {
        captureLaserResult();
        taskResult.reconstruction = failedReconstructionResult("重建失败：发生未知异常。");
    }
    return taskResult;
}

} // namespace

namespace htmsr::app::detail {

inline CameraDeviceInfo findSelectedCameraDevice(const std::vector<CameraDeviceInfo>& devices, const std::string& deviceId)
{
    for (const auto& device : devices) {
        if (device.id == deviceId) {
            return device;
        }
    }
    throw std::runtime_error("Camera device was not found, id=" + deviceId);
}

inline QString directionText(GalvoScanDirection direction)
{
    return direction == GalvoScanDirection::Forward ? QString::fromUtf8("正向") : QString::fromUtf8("反向");
}

inline QString syncModeText(GalvoSyncMode mode)
{
    return mode == GalvoSyncMode::Sync ? QString::fromUtf8("振镜同步") : QString::fromUtf8("振镜异步");
}

inline std::vector<unsigned char> parseHexCommandText(const QString& commandText)
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

inline QString bytesToHexText(const std::vector<unsigned char>& bytes)
{
    QStringList parts;
    for (unsigned char byte : bytes) {
        parts << QString("%1").arg(static_cast<unsigned int>(byte), 2, 16, QChar('0')).toUpper();
    }
    return parts.join(' ');
}

inline bool shouldRefreshPreviewAfterRawCommand(const std::vector<unsigned char>& command)
{
    if (command.size() < 4) {
        return false;
    }

    const unsigned char operation = command[3];
    return operation == 0x04 || operation == 0x05 || operation == 0x09 ||
        operation == 0x1A || operation == 0x1B;
}

inline GalvoMotionVerificationResult configureAndVerifyGalvoMotionParameters(const GalvoScanConfig& galvoConfig, CancellationToken cancellation)
{
    SerialGalvoController controller(galvoConfig);
    try {
        const auto motion = configureAndVerifyGalvoMotion(controller, galvoConfig, [cancellation](int ms) { cancellation.wait(ms); });
        controller.disconnect();
        return {
            true,
            QString::fromUtf8("振镜已连接，参数重新设置并验证通过：步进 %1°，自动旋转 %2°")
                .arg(motion.stepAngleDeg, 0, 'f', 4)
                .arg(motion.totalRotationAngleDeg),
            motion.stepAngleDeg,
            motion.totalRotationAngleDeg,
            QString::fromStdString(motion.portName)
        };
    } catch (const std::exception& ex) {
        controller.disconnect();
        return {false, QString::fromUtf8("振镜连接或参数回读异常：%1").arg(QString::fromUtf8(ex.what()))};
    } catch (...) {
        controller.disconnect();
        return {false, QString::fromUtf8("振镜连接或参数回读发生未知异常。")};
    }
}

} // namespace
