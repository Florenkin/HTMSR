#include "app/acquisition/HikStereoCameraProvider.h"

#include "core/Logger.h"

#include <stdexcept>
#include <utility>

namespace htmsr::app {

/*
    函数功能：构造海康双相机采集源，并接管左右设备对象与采集配置
    输入：
        leftDevice：左相机设备对象
        rightDevice：右相机设备对象
        config：双相机采集任务配置
    输出：
        无（构造后保存左右设备和采集配置，尚未真正连接相机）
*/
HikStereoCameraProvider::HikStereoCameraProvider(CameraDevicePtr leftDevice, CameraDevicePtr rightDevice, StereoCameraConfig config)
    : leftDevice_(std::move(leftDevice))
    , rightDevice_(std::move(rightDevice))
    , config_(std::move(config))
{
}

// 析构时统一停止取流并断开左右设备，避免采集异常退出后相机句柄泄漏。
HikStereoCameraProvider::~HikStereoCameraProvider()
{
    if (leftDevice_) {
        leftDevice_->stopGrabbing();
        leftDevice_->disconnect();
    }
    if (rightDevice_) {
        rightDevice_->stopGrabbing();
        rightDevice_->disconnect();
    }
}

// 采集源名称用于日志和后续可能的 UI 状态展示。
std::string HikStereoCameraProvider::name() const
{
    return "Hik Stereo Camera";
}

bool HikStereoCameraProvider::hasNext() const
{
    return index_ < config_.frameCount;
}

/*
    函数功能：从左右海康相机抓取一组同步帧
    输入：
        无
    输出：
        返回值：一组左右图像帧及其帧序号
*/
FramePair HikStereoCameraProvider::next()
{
    prepare();
    if (!hasNext()) {
        throw std::runtime_error("Hik stereo provider has no more frames.");
    }

    // 左右相机在硬触发模式下会等待外部触发；开发阶段也可用软件触发/连续模式抓取一组帧。
    FramePair pair;
    pair.frameIndex = index_;
    pair.left = leftDevice_->grabFrame(config_.leftParameters.grabTimeoutMs);
    pair.right = rightDevice_->grabFrame(config_.rightParameters.grabTimeoutMs);
    ++index_;
    return pair;
}

// 重置只回退内部帧计数，设备连接和取流状态由 prepare/析构统一管理。
void HikStereoCameraProvider::reset()
{
    index_ = 0;
}

/*
    函数功能：在第一次取帧前完成左右相机连接、参数下发和开始取流
    输入：
        无
    输出：
        无（函数成功后采集源进入可持续抓帧状态，失败时抛出异常）
*/
void HikStereoCameraProvider::prepare()
{
    if (prepared_) {
        return;
    }
    if (!leftDevice_ || !rightDevice_) {
        throw std::runtime_error("Hik stereo provider requires both left and right devices.");
    }

    if (!leftDevice_->connect() || !rightDevice_->connect()) {
        throw std::runtime_error("Failed to connect Hik stereo cameras.");
    }
    if (!leftDevice_->configure(config_.leftParameters) || !rightDevice_->configure(config_.rightParameters)) {
        throw std::runtime_error("Failed to configure Hik stereo cameras.");
    }
    if (!leftDevice_->startGrabbing() || !rightDevice_->startGrabbing()) {
        throw std::runtime_error("Failed to start Hik stereo grabbing.");
    }

    prepared_ = true;
    Logger::instance().info("HikStereo", "Hik stereo provider prepared.");
}

} // namespace htmsr::app
