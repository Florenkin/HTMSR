#include "app/services/IntegratedScanService.h"

#include "app/services/ReconstructionCaptureSessionService.h"
#include "app/services/ReconstructionStorage.h"
#include "core/CalibrationService.h"
#include "core/Logger.h"
#include "core/ReconstructionService.h"

#include <utility>

namespace htmsr::app {

IntegratedWorkflowResult IntegratedScanService::runScanAndReconstruct(
    const IntegratedScanConfig& config, ProgressCallback progressCallback) const
{
    IntegratedWorkflowResult result;
    if (config.forceRecalibration) {
        result.message = "Force recalibration is enabled. Run auto calibration workflow first.";
        Logger::instance().warning("IntegratedWorkflow", result.message);
        return result;
    }
    if (config.stereoCamera.useMockProvider) {
        result.message = "Integrated scan and reconstruct requires real cameras. Disable mock provider first.";
        return result;
    }
    CalibrationService calibrationService;
    Logger::instance().info("IntegratedWorkflow", "Starting integrated scan and reconstruction workflow.");
    if (!calibrationService.loadCalibration(config.calibrationFile, result.calibration) || !result.calibration.isValid()) {
        result.message = "No valid calibration file was found. Run auto calibration workflow first.";
        Logger::instance().warning("IntegratedWorkflow", result.message);
        return result;
    }
    result.usedExistingCalibration = true;

    // 共用已校验的扫描采集路径，避免一键重建仍使用旧的串行取图/漏帧后继续配对逻辑。
    ReconstructionCaptureSessionService captureService;
    result.acquisition = captureService.capture(config, std::move(progressCallback));
    if (!result.acquisition.success) {
        result.message = result.acquisition.message;
        Logger::instance().warning("IntegratedWorkflow", result.message);
        return result;
    }

    ReconstructionInput reconstructionInput;
    reconstructionInput.leftDirectory = result.acquisition.leftDirectory;
    reconstructionInput.rightDirectory = result.acquisition.rightDirectory;
    reconstructionInput.imageRange = config.reconstructionRange;
    reconstructionInput.calibration = result.calibration;
    reconstructionInput.laserConfig = config.laserConfig;
    reconstructionInput.matchDistanceThreshold = config.matchDistanceThreshold;
    ReconstructionService reconstructionService;
    result.reconstruction = reconstructionService.reconstruct(reconstructionInput);
    ReconstructionStorage::savePointClouds(config.stereoCamera.outputDirectory, result.reconstruction,
        result.acquisition.sessionDirectory);
    result.success = result.reconstruction.success;
    result.message = result.success
        ? "Integrated scan and reconstruction finished. Points=" + std::to_string(result.reconstruction.mergedPoints.size())
        : result.reconstruction.message;
    if (result.success) {
        Logger::instance().info("IntegratedWorkflow", result.message);
    } else {
        Logger::instance().warning("IntegratedWorkflow", result.message);
    }
    return result;
}

} // namespace htmsr::app
