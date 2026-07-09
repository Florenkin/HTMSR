#pragma once

#include "core/LaserExtractionService.h"
#include "core/PointCloudService.h"
#include "core/Types.h"

namespace htmsr {

class ReconstructionService {
public:
    ReconstructionResult reconstruct(const ReconstructionInput& input) const;

private:
    std::vector<Eigen::Vector3d> reconstructFrame(
        const cv::Mat& leftImage,
        const cv::Mat& rightImage,
        const CalibrationResult& calibration,
        const LaserExtractionConfig& laserConfig,
        double matchDistanceThreshold,
        cv::Mat& leftPreview,
        cv::Mat& rightPreview) const;

    Eigen::Vector3d pixelToRay(const Eigen::Vector2d& pixel, const Eigen::Matrix3d& cameraMatrix) const;
    Eigen::Vector3d closestPointBetweenLines(
        const Eigen::Vector3d& directionA,
        const Eigen::Vector3d& pointA,
        const Eigen::Vector3d& directionB,
        const Eigen::Vector3d& pointB) const;
};

} // namespace htmsr
