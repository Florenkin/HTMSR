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

// 日志消息对象，用于在核心算法、应用服务和 Qt 日志面板之间传递统一日志。
struct LogMessage {
    LogLevel level = LogLevel::Info;
    std::string module;
    std::string text;
};

// 激光中心线提取算法类型。
enum class LaserExtractionMode {
    GrayCentroid,
    Steger
};

// 激光颜色通道，彩色图像会按指定通道转换为单通道灰度图。
enum class LaserColor {
    Red,
    Green,
    Blue,
    Gray
};

// 图像索引范围，begin/end 均为 -1 时表示处理目录下全部图像。
struct ImageRange {
    int begin = -1;
    int end = -1;
};

// 双目标定输入参数。
struct CalibrationInput {
    std::string leftDirectory;
    std::string rightDirectory;
    cv::Size boardSize = cv::Size(11, 8);
    cv::Size2d squareSize = cv::Size2d(15.0, 15.0);
    ImageRange imageRange;
    std::string outputFile = "stereo_calibration.yml";
};

// 双目标定输出结果，保存左右相机内参、畸变、双目外参和误差统计。
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

// 激光中心线提取参数。
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

// 单幅图像的激光中心线提取结果。
struct LaserExtractionResult {
    std::vector<Eigen::Vector2d> points;
    cv::Mat preview;
};

// 离线重建输入参数。
struct ReconstructionInput {
    std::string leftDirectory;
    std::string rightDirectory;
    ImageRange imageRange;
    CalibrationResult calibration;
    LaserExtractionConfig laserConfig;
    double matchDistanceThreshold = 0.1;
};

// 单帧左右图像重建结果。
struct FrameReconstructionResult {
    std::string leftImagePath;
    std::string rightImagePath;
    std::vector<Eigen::Vector3d> points;
    cv::Mat leftLinePreview;
    cv::Mat rightLinePreview;
};

// 批量重建结果，包含逐帧点云、合并点云以及导出路径。
struct ReconstructionResult {
    std::vector<FrameReconstructionResult> frames;
    std::vector<Eigen::Vector3d> mergedPoints;
    std::string txtPath;
    std::string pcdPath;
};

// 软件工程配置，用于 UI 参数持久化和默认参数恢复。
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
