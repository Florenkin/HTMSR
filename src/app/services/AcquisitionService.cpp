#include "app/services/AcquisitionService.h"

#include "app/acquisition/HikStereoCameraProvider.h"
#include "app/acquisition/MockAcquisitionProvider.h"
#include "core/Logger.h"

#if HTMSR_WITH_HIK_CAMERA
#include "app/acquisition/HikCameraDevice.h"
#endif

#include <QDateTime>
#include <QDir>

#include <opencv2/imgcodecs.hpp>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace htmsr::app {
namespace {

// 统一生成按序号递增的离线保存文件名，便于后续左右目录稳定配对。
std::string frameFileName(int frameIndex)
{
    std::ostringstream stream;
    stream << "frame_" << std::setw(6) << std::setfill('0') << (frameIndex + 1) << ".bmp";
    return stream.str();
}

// 采集会话开始前确保输出目录存在，避免写图时因目录缺失直接失败。
void ensureDirectory(const QString& directory)
{
    QDir dir(directory);
    if (dir.exists()) {
        return;
    }
    if (!dir.mkpath(".")) {
        throw std::runtime_error("Failed to create directory: " + directory.toStdString());
    }
}

// 每次采集创建独立的会话目录，并提前生成 left/right 子目录。
QString createSessionDirectory(const std::string& outputDirectory)
{
    const QString root = QString::fromStdString(outputDirectory.empty() ? "." : outputDirectory);
    ensureDirectory(root);

    const QString sessionName = "capture_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString sessionDirectory = QDir(root).filePath(sessionName);
    ensureDirectory(sessionDirectory);
    ensureDirectory(QDir(sessionDirectory).filePath("left"));
    ensureDirectory(QDir(sessionDirectory).filePath("right"));
    return sessionDirectory;
}

// 根据公共设备 id 在当前枚举结果中回查对应设备，避免 UI 层持有 SDK 私有对象。
CameraDeviceInfo findDeviceById(const std::vector<CameraDeviceInfo>& devices, const std::string& id)
{
    for (const auto& device : devices) {
        if (device.id == id) {
            return device;
        }
    }
    throw std::runtime_error("Camera device was not found, id=" + id);
}

} // namespace

/*
    函数功能：枚举当前构建中可用的在线采集设备
    输入：
        无
    输出：
        返回值：设备基础信息列表；未启用海康 SDK 时返回空列表
*/
std::vector<CameraDeviceInfo> AcquisitionService::enumerateDevices() const
{
#if HTMSR_WITH_HIK_CAMERA
    return enumerateHikCameraDevices();
#else
    Logger::instance().warning("Acquisition", "Hik camera support is disabled in this build.");
    return {};
#endif
}

/*
    函数功能：按双相机配置执行一次采集会话，并将左右图像保存到会话目录
    输入：
        config：双相机采集配置，包括设备、帧数、输出目录和相机参数
        progressCallback：采集进度回调，可为空
    输出：
        返回值：采集会话结果，包含保存目录、图像路径、预览图和成功失败统计
*/
AcquisitionSessionResult AcquisitionService::capture(const StereoCameraConfig& config, ProgressCallback progressCallback) const
{
    if (config.frameCount <= 0) {
        throw std::runtime_error("Capture frame count must be greater than zero.");
    }

    AcquisitionSessionResult result;
    const QString sessionDirectory = createSessionDirectory(config.outputDirectory);
    result.sessionDirectory = sessionDirectory.toStdString();
    result.leftDirectory = QDir(sessionDirectory).filePath("left").toStdString();
    result.rightDirectory = QDir(sessionDirectory).filePath("right").toStdString();

    Logger::instance().info("CaptureSession", "Capture session created: " + result.sessionDirectory);
    auto provider = createProvider(config);

    for (int frameIndex = 0; frameIndex < config.frameCount && provider->hasNext(); ++frameIndex) {
        // 每次从采集源取出一组同步左右帧，并以统一文件名保存到 left/right 目录。
        FramePair pair = provider->next();
        const QString fileName = QString::fromStdString(frameFileName(frameIndex));
        const QString leftPath = QDir(QString::fromStdString(result.leftDirectory)).filePath(fileName);
        const QString rightPath = QDir(QString::fromStdString(result.rightDirectory)).filePath(fileName);

        if (pair.left.empty() || pair.right.empty()) {
            ++result.failedFrameCount;
            Logger::instance().warning("CaptureSession", "Captured empty frame pair, frame=" + std::to_string(frameIndex + 1));
            continue;
        }

        const bool leftSaved = cv::imwrite(leftPath.toStdString(), pair.left);
        const bool rightSaved = cv::imwrite(rightPath.toStdString(), pair.right);
        if (!leftSaved || !rightSaved) {
            ++result.failedFrameCount;
            Logger::instance().error("CaptureSession", "Failed to save frame pair, frame=" + std::to_string(frameIndex + 1));
            continue;
        }

        result.leftImagePaths.push_back(leftPath.toStdString());
        result.rightImagePaths.push_back(rightPath.toStdString());
        result.lastLeftPreview = pair.left.clone();
        result.lastRightPreview = pair.right.clone();
        ++result.capturedFrameCount;

        if (progressCallback) {
            progressCallback(frameIndex + 1, config.frameCount, pair);
        }
    }

    result.success = result.capturedFrameCount > 0;
    result.message = result.success
        ? "Capture finished, frames=" + std::to_string(result.capturedFrameCount)
        : "Capture finished without valid frames.";
    Logger::instance().info("CaptureSession", result.message + ", directory=" + result.sessionDirectory);
    return result;
}

/*
    函数功能：根据采集配置创建具体的图像采集源
    输入：
        config：双相机采集配置
    输出：
        返回值：采集源抽象接口对象，可能是 Mock 采集源或海康双相机采集源
*/
AcquisitionProviderPtr AcquisitionService::createProvider(const StereoCameraConfig& config) const
{
    if (config.useMockProvider) {
        Logger::instance().info("Acquisition", "Using mock stereo acquisition provider.");
        return std::make_unique<MockAcquisitionProvider>(config.frameCount);
    }

#if HTMSR_WITH_HIK_CAMERA
    const auto devices = enumerateHikCameraDevices();
    const auto leftInfo = findDeviceById(devices, config.leftDeviceId);
    const auto rightInfo = findDeviceById(devices, config.rightDeviceId);
    return std::make_unique<HikStereoCameraProvider>(
        std::make_unique<HikCameraDevice>(leftInfo),
        std::make_unique<HikCameraDevice>(rightInfo),
        config);
#else
    throw std::runtime_error("Hik camera support is disabled. Enable HTMSR_ENABLE_HIK_CAMERA or use mock provider.");
#endif
}

} // namespace htmsr::app
