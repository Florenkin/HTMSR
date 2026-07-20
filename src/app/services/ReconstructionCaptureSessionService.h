#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <functional>

namespace htmsr::app {

class ReconstructionCaptureSessionService {
public:
    using ProgressCallback = std::function<void(int currentFrame, int totalFrames)>;

    AcquisitionSessionResult capture(const IntegratedScanConfig& config, ProgressCallback progressCallback = {}) const;
};

} // namespace htmsr::app
