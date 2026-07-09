#pragma once

#include "core/Types.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace htmsr {

class CalibrationService {
public:
    CalibrationResult calibrate(const CalibrationInput& input) const;
    bool loadCalibration(const std::string& filename, CalibrationResult& result) const;
    void saveCalibration(const std::string& filename, const CalibrationResult& result) const;

private:
    struct CameraCalibration {
        cv::Mat cameraMatrix;
        cv::Mat distortion;
        std::vector<double> perImageErrors;
        int successCount = 0;
        int failureCount = 0;
    };

    CameraCalibration calibrateSingleCamera(
        const std::vector<cv::Mat>& images,
        const cv::Size& boardSize,
        const cv::Size2d& squareSize,
        const std::string& cameraName) const;

    std::vector<cv::Point3f> createObjectPoints(const cv::Size& boardSize, const cv::Size2d& squareSize) const;
};

} // namespace htmsr
