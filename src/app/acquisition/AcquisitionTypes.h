#pragma once

#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace htmsr::app {

enum class CameraState {
    Disconnected,
    Connected,
    Streaming,
    Error
};

struct FramePair {
    cv::Mat left;
    cv::Mat right;
    std::string leftPath;
    std::string rightPath;
};

class ICameraDevice {
public:
    virtual ~ICameraDevice() = default;
    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual CameraState state() const = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
};

class IAcquisitionProvider {
public:
    virtual ~IAcquisitionProvider() = default;
    virtual std::string name() const = 0;
    virtual bool hasNext() const = 0;
    virtual FramePair next() = 0;
    virtual void reset() = 0;
};

using AcquisitionProviderPtr = std::unique_ptr<IAcquisitionProvider>;

std::string toString(CameraState state);

} // namespace htmsr::app
