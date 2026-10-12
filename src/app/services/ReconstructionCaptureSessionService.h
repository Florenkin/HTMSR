#pragma once
#include "core/Cancellation.h"

#include "app/acquisition/AcquisitionTypes.h"

#include <functional>

namespace htmsr::app {

class ReconstructionCaptureSessionService {
public:
    using ProgressCallback = std::function<void(int currentFrame, int totalFrames, const FramePair& frame)>;

    AcquisitionSessionResult capture(const IntegratedScanConfig& config, ProgressCallback progressCallback = {}, CancellationToken cancellation = {}) const;
};

} // namespace htmsr::app
