#include "core/CalibrationService.h"
#include <filesystem>
#include <fstream>

#include "core/FileSystemUtils.h"
#include "core/Logger.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <sstream>
#include <stdexcept>

namespace htmsr {
namespace {

struct CalibrationImageObservation {
    cv::Mat gray;
    std::vector<cv::Point2f> corners;
    bool found = false;
    int imageNumber = 0;
};

cv::Mat toGray(const cv::Mat& input)
{
    // 标定角点检测统一使用灰度图，彩色图在这里转换为单通道图像。
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

cv::Mat enhanceForChessboardDetection(const cv::Mat& gray)
{
    if (gray.empty()) {
        return {};
    }

    cv::Mat detectionInput;
    if (gray.type() == CV_8UC1) {
        detectionInput = gray;
    } else {
        cv::normalize(gray, detectionInput, 0, 255, cv::NORM_MINMAX, CV_8U);
    }

    cv::Mat enhanced;
    cv::equalizeHist(detectionInput, enhanced);
    return enhanced;
}

bool findChessboardCornersRobust(
    const cv::Mat& gray,
    const cv::Size& boardSize,
    std::vector<cv::Point2f>& corners)
{
    if (gray.empty()) {
        return false;
    }

    const cv::Mat enhanced = enhanceForChessboardDetection(gray);
    if (cv::findChessboardCorners(
            enhanced,
            boardSize,
            corners,
            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE)) {
        return true;
    }

    return cv::findChessboardCornersSB(
        enhanced,
        boardSize,
        corners,
        cv::CALIB_CB_NORMALIZE_IMAGE);
}

double computeReprojectionError(
    const std::vector<cv::Point3f>& objectPoints,
    const std::vector<cv::Point2f>& imagePoints,
    const cv::Mat& rvec,
    const cv::Mat& tvec,
    const cv::Mat& cameraMatrix,
    const cv::Mat& distortion)
{
    // 将三维棋盘角点重新投影到图像平面，用平均像素距离评价标定误差。
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
    // 逐个读取图像，读取失败的文件只写日志，不中断整个标定流程。
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

std::vector<CalibrationImageObservation> detectCalibrationImageCorners(
    const std::vector<std::string>& paths,
    const cv::Size& boardSize,
    const std::string& cameraName, CancellationToken cancellation)
{
    std::vector<CalibrationImageObservation> observations;
    observations.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        cancellation.check();
        CalibrationImageObservation observation;
        observation.imageNumber = static_cast<int>(i + 1);
        observation.gray = toGray(cv::imread(paths[i], cv::IMREAD_COLOR));

        if (observation.gray.empty()) {
            Logger::instance().warning("Calibration", "Failed to read image: " + paths[i]);
            observations.push_back(observation);
            continue;
        }

        observation.found = findChessboardCornersRobust(observation.gray, boardSize, observation.corners);
        if (observation.found) {
            cv::cornerSubPix(observation.gray, observation.corners, cv::Size(15, 15), cv::Size(-1, -1),
                cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
        } else {
            Logger::instance().warning(
                "Calibration",
                cameraName + " camera corners not found in image index " + std::to_string(i) +
                    " (frame " + std::to_string(i + 1) + ")");
        }

        observations.push_back(observation);
    }
    return observations;
}

std::vector<std::vector<cv::Point2f>> collectSuccessfulCorners(const std::vector<CalibrationImageObservation>& observations)
{
    std::vector<std::vector<cv::Point2f>> corners;
    for (const auto& observation : observations) {
        if (observation.found) {
            corners.push_back(observation.corners);
        }
    }
    return corners;
}

std::vector<int> collectFailedImageNumbers(const std::vector<CalibrationImageObservation>& observations)
{
    std::vector<int> failedImageNumbers;
    for (const auto& observation : observations) {
        if (!observation.found) {
            failedImageNumbers.push_back(observation.imageNumber);
        }
    }
    return failedImageNumbers;
}

cv::Size firstLoadedImageSize(const std::vector<CalibrationImageObservation>& observations)
{
    for (const auto& observation : observations) {
        if (!observation.gray.empty()) {
            return observation.gray.size();
        }
    }
    return {};
}

std::string joinImageNumbers(const std::vector<int>& imageNumbers)
{
    if (imageNumbers.empty()) {
        return "none";
    }

    std::ostringstream stream;
    for (size_t i = 0; i < imageNumbers.size(); ++i) {
        if (i > 0) {
            stream << ", ";
        }
        stream << imageNumbers[i];
    }
    return stream.str();
}

std::string chessboardHint(const cv::Size& boardSize)
{
    std::ostringstream stream;
    stream << "Configured board inner corners=" << boardSize.width << "x" << boardSize.height
           << ". Check the printed board inner-corner count, move the board between captures, "
              "and keep the full chessboard sharp and inside both camera views.";
    return stream.str();
}

} // namespace

CalibrationResult CalibrationService::calibrate(const CalibrationInput& input, CancellationToken cancellation) const
{
    cancellation.check();
    Logger::instance().info("Calibration", "Starting stereo calibration.");
    const auto calibrationStart = std::chrono::steady_clock::now();

    // 读取左右标定图像路径，并按较短的一侧组成有效图像对。
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
    const auto cornerStart = std::chrono::steady_clock::now();
    auto leftObservationFuture = std::async(std::launch::async, [pairedLeft, boardSize = input.boardSize, cancellation]() {
        return detectCalibrationImageCorners(pairedLeft, boardSize, "Left", cancellation);
    });
    const auto rightObservations = detectCalibrationImageCorners(pairedRight, input.boardSize, "Right", cancellation);
    const auto leftObservations = leftObservationFuture.get();
    const auto cornerEnd = std::chrono::steady_clock::now();
    Logger::instance().info(
        "Calibration",
        "Chessboard detection finished, pairs=" + std::to_string(pairCount) +
            ", elapsedMs=" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(cornerEnd - cornerStart).count()));
    const cv::Size leftImageSize = firstLoadedImageSize(leftObservations);
    const cv::Size rightImageSize = firstLoadedImageSize(rightObservations);

    if (leftImageSize.empty() || rightImageSize.empty()) {
        throw std::runtime_error("Calibration images could not be loaded.");
    }

    // 先分别完成左右单目标定，得到两台相机各自的内参和畸变参数。
    cancellation.check();
    const auto singleCalibrationStart = std::chrono::steady_clock::now();
    auto leftCalibFuture = std::async(std::launch::async, [this, &input, &leftObservations, leftImageSize, pairCount]() {
        return calibrateSingleCamera(
            collectSuccessfulCorners(leftObservations),
            leftImageSize,
            collectFailedImageNumbers(leftObservations),
            pairCount,
            input.boardSize,
            input.squareSize,
            "Left");
    });
    const auto rightCalib = calibrateSingleCamera(
        collectSuccessfulCorners(rightObservations),
        rightImageSize,
        collectFailedImageNumbers(rightObservations),
        pairCount,
        input.boardSize,
        input.squareSize,
        "Right");
    const auto leftCalib = leftCalibFuture.get();
    const auto singleCalibrationEnd = std::chrono::steady_clock::now();
    Logger::instance().info(
        "Calibration",
        "Single-camera calibration finished, elapsedMs=" +
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(singleCalibrationEnd - singleCalibrationStart).count()));

    cancellation.check();
    std::vector<std::vector<cv::Point3f>> stereoObjectPoints;
    std::vector<std::vector<cv::Point2f>> leftImagePoints;
    std::vector<std::vector<cv::Point2f>> rightImagePoints;
    const auto objectPoints = createObjectPoints(input.boardSize, input.squareSize);

    // 双目标定要求同一时刻的左右图都成功检测到棋盘角点。
    int failures = 0;
    std::vector<int> failedStereoPairNumbers;
    for (int i = 0; i < pairCount; ++i) {
        const bool leftFound = leftObservations[i].found;
        const bool rightFound = rightObservations[i].found;
        const cv::Mat& leftGray = leftObservations[i].gray;
        const cv::Mat& rightGray = rightObservations[i].gray;
        std::vector<cv::Point2f> leftCorners = leftObservations[i].corners;
        std::vector<cv::Point2f> rightCorners = rightObservations[i].corners;

        if (!leftFound || !rightFound) {
            ++failures;
            failedStereoPairNumbers.push_back(i + 1);
            Logger::instance().warning(
                "Calibration",
                "Stereo corners not found in pair index " + std::to_string(i) +
                    " (frame " + std::to_string(i + 1) + "). left=" +
                    std::string(leftFound ? "yes" : "no") +
                    ", right=" + std::string(rightFound ? "yes" : "no"));
            continue;
        }

        // 继续做亚像素优化，提高双目外参求解精度。
        cv::cornerSubPix(leftGray, leftCorners, cv::Size(15, 15), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
        cv::cornerSubPix(rightGray, rightCorners, cv::Size(15, 15), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));

        stereoObjectPoints.push_back(objectPoints);
        leftImagePoints.push_back(leftCorners);
        rightImagePoints.push_back(rightCorners);
    }

    if (stereoObjectPoints.empty()) {
        throw std::runtime_error(
            "Stereo calibration failed: no valid left/right chessboard pairs were found. Failed frames: " +
            joinImageNumbers(failedStereoPairNumbers) + ". " + chessboardHint(input.boardSize));
    }
    if (stereoObjectPoints.size() < 3) {
        throw std::runtime_error(
            "Stereo calibration failed: only " + std::to_string(stereoObjectPoints.size()) +
            " valid stereo chessboard pair(s) were found. Need at least 3, preferably 10 or more. Failed frames: " +
            joinImageNumbers(failedStereoPairNumbers) + ". " + chessboardHint(input.boardSize));
    }

    CalibrationResult result;
    // 单目标定结果作为双目标定初值，stereoCalibrate 中使用 CALIB_FIX_INTRINSIC 固定内参。
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

    cv::Size imageSize = leftImageSize;
    cancellation.check();
    const auto stereoCalibrationStart = std::chrono::steady_clock::now();
    // 在已知左右内参的基础上求解双目旋转、平移、本质矩阵和基础矩阵。
    try {
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
        const auto stereoCalibrationEnd = std::chrono::steady_clock::now();
        Logger::instance().info(
            "Calibration",
            "Stereo solve finished, validPairs=" + std::to_string(result.successfulPairs) +
                ", elapsedMs=" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(stereoCalibrationEnd - stereoCalibrationStart).count()));
    } catch (const cv::Exception& ex) {
        throw std::runtime_error(
            "OpenCV stereo calibration failed after detecting " +
            std::to_string(stereoObjectPoints.size()) + " valid pair(s). " +
            chessboardHint(input.boardSize) + " OpenCV: " + ex.what());
    }

    Logger::instance().info("Calibration", "Stereo calibration completed. RMS=" + std::to_string(result.rms));
    const auto calibrationEnd = std::chrono::steady_clock::now();
    Logger::instance().info(
        "Calibration",
        "Calibration total elapsedMs=" +
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(calibrationEnd - calibrationStart).count()));
    cancellation.check();
    if (!input.outputFile.empty()) {
        saveCalibration(input.outputFile, result);
    }
    return result;
}

bool CalibrationService::loadCalibration(const std::string& filename, CalibrationResult& result) const
{
    try {
        // 启动自动恢复也会读取此文件；先拒绝明显不是 OpenCV 存储格式的文本，
        // 避免把普通文本传入旧版 FileStorage 的错误路径。
        std::ifstream input(std::filesystem::u8path(filename), std::ios::binary);
        if (!input) {
            Logger::instance().error("Calibration", "Failed to open calibration file: " + filename);
            return false;
        }
        char buffer[64] = {};
        input.read(buffer, sizeof(buffer));
        std::string header(buffer, static_cast<size_t>(input.gcount()));
        input.close();
        const bool compressed = header.size() >= 2 &&
            static_cast<unsigned char>(header[0]) == 0x1f && static_cast<unsigned char>(header[1]) == 0x8b;
        if (header.compare(0, 3, "\xef\xbb\xbf") == 0) header.erase(0, 3);
        const auto start = header.find_first_not_of(" \t\r\n");
        if (!compressed && (start == std::string::npos ||
            (header.compare(start, 5, "%YAML") != 0 && header[start] != '<' && header[start] != '{'))) {
            Logger::instance().error("Calibration", "Invalid calibration file format: " + filename);
            return false;
        }
        // 保持 OpenCV FileStorage 格式，兼容参考代码生成的 yml 标定文件。
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

        // 旧标定文件可能没有 P1/P2，这里使用相机内参作为默认投影矩阵。
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
    } catch (const cv::Exception& ex) {
        Logger::instance().error("Calibration", "OpenCV failed to load calibration file: " + filename + ", " + ex.what());
        result = CalibrationResult{};
        return false;
    } catch (const std::exception& ex) {
        Logger::instance().error("Calibration", "Failed to load calibration file: " + filename + ", " + ex.what());
        result = CalibrationResult{};
        return false;
    }
}

void CalibrationService::saveCalibration(const std::string& filename, const CalibrationResult& result, bool logSave) const
{
    // 写文件前先创建父目录，避免用户选择新输出目录时保存失败。
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

    fs.release();
    if (logSave) {
        Logger::instance().info("Calibration", "Calibration file saved: " + filename);
    }
}

CalibrationService::CameraCalibration CalibrationService::calibrateSingleCamera(
    const std::vector<std::vector<cv::Point2f>>& detectedImagePoints,
    cv::Size detectedImageSize,
    const std::vector<int>& detectedFailedImageNumbers,
    int totalImageCount,
    const cv::Size& boardSize,
    const cv::Size2d& squareSize,
    const std::string& cameraName) const
{
    // 单目标定流程：提取角点、生成对应世界坐标、执行 calibrateCamera 并计算误差。
    std::vector<std::vector<cv::Point2f>> imagePoints;
    std::vector<std::vector<cv::Point3f>> objectPoints;
    const auto objectTemplate = createObjectPoints(boardSize, squareSize);
    const std::vector<cv::Mat> images;

    cv::Size imageSize;
    int failures = 0;
    std::vector<int> failedImageNumbers;
    for (size_t i = 0; i < images.size(); ++i) {
        const auto gray = toGray(images[i]);
        if (gray.empty()) {
            ++failures;
            failedImageNumbers.push_back(static_cast<int>(i + 1));
            continue;
        }
        imageSize = gray.size();

        std::vector<cv::Point2f> corners;
        const bool found = findChessboardCornersRobust(gray, boardSize, corners);

        if (!found) {
            ++failures;
            failedImageNumbers.push_back(static_cast<int>(i + 1));
            Logger::instance().warning(
                "Calibration",
                cameraName + " camera corners not found in image index " + std::to_string(i) +
                    " (frame " + std::to_string(i + 1) + ")");
            continue;
        }

        // 对粗提取角点继续做亚像素优化。
        cv::cornerSubPix(gray, corners, cv::Size(15, 15), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
        imagePoints.push_back(corners);
        objectPoints.push_back(objectTemplate);
    }

    imagePoints = detectedImagePoints;
    imageSize = detectedImageSize;
    failedImageNumbers = detectedFailedImageNumbers;
    failures = std::max(0, totalImageCount - static_cast<int>(imagePoints.size()));
    objectPoints.assign(imagePoints.size(), objectTemplate);

    if (imagePoints.empty()) {
        throw std::runtime_error(
            cameraName + " camera calibration failed: no valid chessboard images. Failed frames: " +
            joinImageNumbers(failedImageNumbers) + ". " + chessboardHint(boardSize));
    }
    if (imagePoints.size() < 3) {
        throw std::runtime_error(
            cameraName + " camera calibration failed: only " + std::to_string(imagePoints.size()) +
            " valid chessboard image(s) were found. Need at least 3, preferably 10 or more. Failed frames: " +
            joinImageNumbers(failedImageNumbers) + ". " + chessboardHint(boardSize));
    }

    CameraCalibration result;
    result.cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    result.distortion = cv::Mat::zeros(1, 5, CV_64F);
    result.failureCount = failures;
    result.failedImageNumbers = failedImageNumbers;
    result.successCount = static_cast<int>(imagePoints.size());

    std::vector<cv::Mat> rvecs;
    std::vector<cv::Mat> tvecs;
    // OpenCV 单目标定，求解相机内参、畸变参数和每幅图的外参。
    try {
        cv::calibrateCamera(objectPoints, imagePoints, imageSize, result.cameraMatrix, result.distortion, rvecs, tvecs);
    } catch (const cv::Exception& ex) {
        throw std::runtime_error(
            cameraName + " camera OpenCV calibration failed after detecting " +
            std::to_string(imagePoints.size()) + " valid image(s). " +
            chessboardHint(boardSize) + " OpenCV: " + ex.what());
    }

    // 对每一幅有效图像单独计算重投影误差，便于后续 UI 展示和质量判断。
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
    // 假设标定板位于世界坐标系 z=0 平面，每个角点按实际方格尺寸排列。
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
