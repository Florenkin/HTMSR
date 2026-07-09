#include "core/CalibrationService.h"

#include "core/FileSystemUtils.h"
#include "core/Logger.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace htmsr {
namespace {

cv::Mat toGray(const cv::Mat& input)
{
    if (input.empty()) {
        return {};
    }
    if (input.channels() == 1) {
        return input.clone();
    }
    cv::Mat gray;
    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

double computeReprojectionError(
    const std::vector<cv::Point3f>& objectPoints,
    const std::vector<cv::Point2f>& imagePoints,
    const cv::Mat& rvec,
    const cv::Mat& tvec,
    const cv::Mat& cameraMatrix,
    const cv::Mat& distortion)
{
    std::vector<cv::Point2f> projected;
    cv::projectPoints(objectPoints, rvec, tvec, cameraMatrix, distortion, projected);

    double total = 0.0;
    for (size_t i = 0; i < imagePoints.size(); ++i) {
        const auto delta = projected[i] - imagePoints[i];
        total += std::sqrt(delta.x * delta.x + delta.y * delta.y);
    }
    return imagePoints.empty() ? 0.0 : total / static_cast<double>(imagePoints.size());
}

std::vector<cv::Mat> readImages(const std::vector<std::string>& paths)
{
    std::vector<cv::Mat> images;
    images.reserve(paths.size());
    for (const auto& path : paths) {
        cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
        if (!image.empty()) {
            images.push_back(image);
        } else {
            Logger::instance().warning("Calibration", "Failed to read image: " + path);
        }
    }
    return images;
}

} // namespace

CalibrationResult CalibrationService::calibrate(const CalibrationInput& input) const
{
    Logger::instance().info("Calibration", "Starting stereo calibration.");

    const auto leftPaths = listImageFiles(input.leftDirectory, input.imageRange);
    const auto rightPaths = listImageFiles(input.rightDirectory, input.imageRange);
    const int pairCount = static_cast<int>(std::min(leftPaths.size(), rightPaths.size()));
    if (pairCount == 0) {
        throw std::runtime_error("No calibration image pairs were found.");
    }
    if (leftPaths.size() != rightPaths.size()) {
        Logger::instance().warning("Calibration", "Left and right calibration image counts differ. Using paired minimum count.");
    }

    std::vector<std::string> pairedLeft(leftPaths.begin(), leftPaths.begin() + pairCount);
    std::vector<std::string> pairedRight(rightPaths.begin(), rightPaths.begin() + pairCount);
    const auto leftImages = readImages(pairedLeft);
    const auto rightImages = readImages(pairedRight);

    if (leftImages.empty() || rightImages.empty()) {
        throw std::runtime_error("Calibration images could not be loaded.");
    }

    const auto leftCalib = calibrateSingleCamera(leftImages, input.boardSize, input.squareSize, "Left");
    const auto rightCalib = calibrateSingleCamera(rightImages, input.boardSize, input.squareSize, "Right");

    std::vector<std::vector<cv::Point3f>> stereoObjectPoints;
    std::vector<std::vector<cv::Point2f>> leftImagePoints;
    std::vector<std::vector<cv::Point2f>> rightImagePoints;
    const auto objectPoints = createObjectPoints(input.boardSize, input.squareSize);

    int failures = 0;
    for (int i = 0; i < pairCount; ++i) {
        cv::Mat leftGray = toGray(cv::imread(pairedLeft[i], cv::IMREAD_COLOR));
        cv::Mat rightGray = toGray(cv::imread(pairedRight[i], cv::IMREAD_COLOR));
        if (leftGray.empty() || rightGray.empty()) {
            ++failures;
            continue;
        }

        std::vector<cv::Point2f> leftCorners;
        std::vector<cv::Point2f> rightCorners;
        const bool leftFound = cv::findChessboardCornersSB(
            leftGray,
            input.boardSize,
            leftCorners,
            cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY);
        const bool rightFound = cv::findChessboardCornersSB(
            rightGray,
            input.boardSize,
            rightCorners,
            cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY);

        if (!leftFound || !rightFound) {
            ++failures;
            Logger::instance().warning("Calibration", "Stereo corners not found in pair index " + std::to_string(i));
            continue;
        }

        cv::cornerSubPix(leftGray, leftCorners, cv::Size(15, 15), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
        cv::cornerSubPix(rightGray, rightCorners, cv::Size(15, 15), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));

        stereoObjectPoints.push_back(objectPoints);
        leftImagePoints.push_back(leftCorners);
        rightImagePoints.push_back(rightCorners);
    }

    if (stereoObjectPoints.empty()) {
        throw std::runtime_error("No valid stereo chessboard pairs were found.");
    }

    CalibrationResult result;
    result.K1 = leftCalib.cameraMatrix.clone();
    result.D1 = leftCalib.distortion.clone();
    result.K2 = rightCalib.cameraMatrix.clone();
    result.D2 = rightCalib.distortion.clone();
    result.P1 = result.K1.clone();
    result.P2 = result.K2.clone();
    result.leftImageErrors = leftCalib.perImageErrors;
    result.rightImageErrors = rightCalib.perImageErrors;
    result.successfulPairs = static_cast<int>(stereoObjectPoints.size());
    result.failedPairs = failures;

    cv::Size imageSize = leftImages.front().size();
    result.rms = cv::stereoCalibrate(
        stereoObjectPoints,
        leftImagePoints,
        rightImagePoints,
        result.K1,
        result.D1,
        result.K2,
        result.D2,
        imageSize,
        result.R,
        result.t,
        result.E,
        result.F,
        cv::CALIB_FIX_INTRINSIC,
        cv::TermCriteria(cv::TermCriteria::COUNT, 30, 1e-6));

    Logger::instance().info("Calibration", "Stereo calibration completed. RMS=" + std::to_string(result.rms));
    if (!input.outputFile.empty()) {
        saveCalibration(input.outputFile, result);
    }
    return result;
}

bool CalibrationService::loadCalibration(const std::string& filename, CalibrationResult& result) const
{
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        Logger::instance().error("Calibration", "Failed to open calibration file: " + filename);
        return false;
    }

    fs["rms"] >> result.rms;
    fs["K1"] >> result.K1;
    fs["D1"] >> result.D1;
    fs["K2"] >> result.K2;
    fs["D2"] >> result.D2;
    fs["P1"] >> result.P1;
    fs["P2"] >> result.P2;
    fs["R"] >> result.R;
    fs["t"] >> result.t;
    fs["E"] >> result.E;
    fs["F"] >> result.F;

    if (result.P1.empty() && !result.K1.empty()) {
        result.P1 = result.K1.clone();
    }
    if (result.P2.empty() && !result.K2.empty()) {
        result.P2 = result.K2.clone();
    }

    const bool valid = result.isValid();
    Logger::instance().log(valid ? LogLevel::Info : LogLevel::Error, "Calibration",
        valid ? "Calibration file loaded: " + filename : "Calibration file is missing required matrices: " + filename);
    return valid;
}

void CalibrationService::saveCalibration(const std::string& filename, const CalibrationResult& result) const
{
    ensureParentDirectory(filename);
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    if (!fs.isOpened()) {
        throw std::runtime_error("Failed to write calibration file: " + filename);
    }

    fs << "rms" << result.rms;
    fs << "K1" << result.K1;
    fs << "D1" << result.D1;
    fs << "K2" << result.K2;
    fs << "D2" << result.D2;
    fs << "P1" << result.P1;
    fs << "P2" << result.P2;
    fs << "R" << result.R;
    fs << "t" << result.t;
    fs << "E" << result.E;
    fs << "F" << result.F;

    Logger::instance().info("Calibration", "Calibration file saved: " + filename);
}

CalibrationService::CameraCalibration CalibrationService::calibrateSingleCamera(
    const std::vector<cv::Mat>& images,
    const cv::Size& boardSize,
    const cv::Size2d& squareSize,
    const std::string& cameraName) const
{
    std::vector<std::vector<cv::Point2f>> imagePoints;
    std::vector<std::vector<cv::Point3f>> objectPoints;
    const auto objectTemplate = createObjectPoints(boardSize, squareSize);

    cv::Size imageSize;
    int failures = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        const auto gray = toGray(images[i]);
        if (gray.empty()) {
            ++failures;
            continue;
        }
        imageSize = gray.size();

        std::vector<cv::Point2f> corners;
        const bool found = cv::findChessboardCornersSB(
            gray,
            boardSize,
            corners,
            cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY);

        if (!found) {
            ++failures;
            Logger::instance().warning("Calibration", cameraName + " camera corners not found in image index " + std::to_string(i));
            continue;
        }

        cv::cornerSubPix(gray, corners, cv::Size(15, 15), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
        imagePoints.push_back(corners);
        objectPoints.push_back(objectTemplate);
    }

    if (imagePoints.empty()) {
        throw std::runtime_error(cameraName + " camera calibration failed: no valid chessboard images.");
    }

    CameraCalibration result;
    result.cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    result.distortion = cv::Mat::zeros(1, 5, CV_64F);
    result.failureCount = failures;
    result.successCount = static_cast<int>(imagePoints.size());

    std::vector<cv::Mat> rvecs;
    std::vector<cv::Mat> tvecs;
    cv::calibrateCamera(objectPoints, imagePoints, imageSize, result.cameraMatrix, result.distortion, rvecs, tvecs);

    result.perImageErrors.reserve(imagePoints.size());
    for (size_t i = 0; i < imagePoints.size(); ++i) {
        result.perImageErrors.push_back(computeReprojectionError(
            objectPoints[i], imagePoints[i], rvecs[i], tvecs[i], result.cameraMatrix, result.distortion));
    }

    Logger::instance().info("Calibration",
        cameraName + " camera calibration valid images=" + std::to_string(result.successCount) +
        ", failed images=" + std::to_string(result.failureCount));
    return result;
}

std::vector<cv::Point3f> CalibrationService::createObjectPoints(const cv::Size& boardSize, const cv::Size2d& squareSize) const
{
    std::vector<cv::Point3f> points;
    points.reserve(static_cast<size_t>(boardSize.width * boardSize.height));
    for (int y = 0; y < boardSize.height; ++y) {
        for (int x = 0; x < boardSize.width; ++x) {
            points.emplace_back(
                static_cast<float>(x * squareSize.width),
                static_cast<float>(y * squareSize.height),
                0.0f);
        }
    }
    return points;
}

} // namespace htmsr
