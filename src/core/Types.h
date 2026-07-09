#pragma once

#include <Eigen/Core>
#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace htmsr {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

struct LogMessage {
    LogLevel level = LogLevel::Info;
    std::string module;
    std::string text;
};

enum class LaserExtractionMode {
    GrayCentroid,
    Steger
};

enum class LaserColor {
    Red,
    Green,
    Blue,
    Gray
};

struct ImageRange {
    int begin = -1;
    int end = -1;
};

struct CalibrationInput {
    std::string leftDirectory;
    std::string rightDirectory;
    cv::Size boardSize = cv::Size(11, 8);
    cv::Size2d squareSize = cv::Size2d(15.0, 15.0);
    ImageRange imageRange;
    std::string outputFile = "stereo_calibration.yml";
};

struct CalibrationResult {
    cv::Mat K1;
    cv::Mat D1;
    cv::Mat K2;
    cv::Mat D2;
    cv::Mat P1;
    cv::Mat P2;
    cv::Mat R;
    cv::Mat t;
    cv::Mat E;
    cv::Mat F;
    double rms = 0.0;
    std::vector<double> leftImageErrors;
    std::vector<double> rightImageErrors;
    int successfulPairs = 0;
    int failedPairs = 0;

    bool isValid() const
    {
        return !K1.empty() && !D1.empty() && !K2.empty() && !D2.empty() && !R.empty() && !t.empty();
    }
};

struct LaserExtractionConfig {
    LaserExtractionMode mode = LaserExtractionMode::GrayCentroid;
    cv::Rect leftRoi = cv::Rect(0, 0, 3072, 2048);
    cv::Rect rightRoi = cv::Rect(0, 0, 3072, 2048);
    int grayThreshold = 150;
    int minGray = 30;
    LaserColor laserColor = LaserColor::Blue;
    double binaryThreshold = 100.0;
    double selectionThreshold = 200.0;
    double stripeWidth = 5.0;
    bool filterStegerPoints = true;
    bool removeEndPoints = false;
    int removeEndPointCount = 10;
};

struct LaserExtractionResult {
    std::vector<Eigen::Vector2d> points;
    cv::Mat preview;
};

struct ReconstructionInput {
    std::string leftDirectory;
    std::string rightDirectory;
    ImageRange imageRange;
    CalibrationResult calibration;
    LaserExtractionConfig laserConfig;
    double matchDistanceThreshold = 0.1;
};

struct FrameReconstructionResult {
    std::string leftImagePath;
    std::string rightImagePath;
    std::vector<Eigen::Vector3d> points;
    cv::Mat leftLinePreview;
    cv::Mat rightLinePreview;
};

struct ReconstructionResult {
    std::vector<FrameReconstructionResult> frames;
    std::vector<Eigen::Vector3d> mergedPoints;
    std::string txtPath;
    std::string pcdPath;
};

struct AppProjectConfig {
    std::string leftCalibrationDirectory;
    std::string rightCalibrationDirectory;
    std::string leftReconstructionDirectory;
    std::string rightReconstructionDirectory;
    std::string calibrationFile = "stereo_calibration.yml";
    std::string outputDirectory = ".";
    CalibrationInput calibrationInput;
    LaserExtractionConfig laserConfig;
    double matchDistanceThreshold = 0.1;
};

} // namespace htmsr
