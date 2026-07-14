#include "app/acquisition/HikStereoCameraProvider.h"

#include "core/Logger.h"

#include <stdexcept>
#include <utility>

namespace htmsr::app {

HikStereoCameraProvider::HikStereoCameraProvider(CameraDevicePtr leftDevice, CameraDevicePtr rightDevice, StereoCameraConfig config)
    : leftDevice_(std::move(leftDevice))
    , rightDevice_(std::move(rightDevice))
    , config_(std::move(config))
{
}

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

std::string HikStereoCameraProvider::name() const
{
    return "Hik Stereo Camera";
}

bool HikStereoCameraProvider::hasNext() const
{
    return index_ < config_.frameCount;
}

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

void HikStereoCameraProvider::reset()
{
    index_ = 0;
}

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
