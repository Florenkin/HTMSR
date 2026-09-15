#include "app/services/IntegratedCalibrationCaptureService.h"

#include "app/services/AcquisitionService.h"
#include "core/CalibrationService.h"
#include "core/Logger.h"

#include <stdexcept>
#include <utility>

namespace htmsr::app {

/*
    函数功能：在线采集左右棋盘格图像并直接执行双目标定
    输入：
        config：自动工作流配置，主要使用其中的双相机采集配置和标定输入配置
    输出：
        返回值：包含采集结果、标定结果和提示信息的工作流结果
*/
IntegratedWorkflowResult IntegratedCalibrationCaptureService::run(const IntegratedScanConfig& config, ProgressCallback progressCallback) const
{
    if (config.stereoCamera.useMockProvider) {
        throw std::runtime_error("Auto calibration requires real cameras. Disable mock provider first.");
    }

    AcquisitionService acquisitionService;
    CalibrationService calibrationService;
    IntegratedWorkflowResult result;
    StereoCameraConfig cameraConfig = config.stereoCamera;
    cameraConfig.leftParameters.useHardwareTrigger = false;
    cameraConfig.rightParameters.useHardwareTrigger = false;

    Logger::instance().info("IntegratedWorkflow", "Starting auto calibration workflow.");
    result.acquisition = acquisitionService.capture(cameraConfig, std::move(progressCallback));
    if (!result.acquisition.success) {
        result.success = false;
        result.message = "Auto calibration capture did not produce valid image pairs.";
        Logger::instance().warning("IntegratedWorkflow", result.message);
        return result;
    }

    CalibrationInput calibrationInput = config.calibrationInput;
    calibrationInput.leftDirectory = result.acquisition.leftDirectory;
    calibrationInput.rightDirectory = result.acquisition.rightDirectory;
    result.calibration = calibrationService.calibrate(calibrationInput);
    result.success = result.calibration.isValid();
    result.message = result.success
        ? "Auto calibration finished. RMS=" + std::to_string(result.calibration.rms)
        : "Auto calibration finished without a valid calibration result.";
    Logger::instance().info("IntegratedWorkflow", result.message);
    return result;
}

} // namespace htmsr::app
