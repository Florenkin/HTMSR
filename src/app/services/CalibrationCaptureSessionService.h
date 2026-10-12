#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <memory>

namespace htmsr::app {

class CalibrationCaptureSessionService {
public:
    CalibrationCaptureSessionService();
    ~CalibrationCaptureSessionService();

    CalibrationCaptureSessionService(const CalibrationCaptureSessionService&) = delete;
    CalibrationCaptureSessionService& operator=(const CalibrationCaptureSessionService&) = delete;

    CalibrationCaptureSessionState start(const StereoCameraConfig& config);
    // 第一次采集自动建立会话，后续采集复用同一组相机和保存目录。
    AcquisitionSessionResult captureCurrentFrame(const StereoCameraConfig& config);
    AcquisitionSessionResult captureCurrentFrame();
    FramePair grabPreviewFrame();
    void finish();
    bool isActive() const;
    AcquisitionSessionResult currentResult() const;
    void setCurrentResult(const AcquisitionSessionResult& result);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace htmsr::app
