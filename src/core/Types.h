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
    cv::Size boardSize = cv::Size(9, 6);
    cv::Size2d squareSize = cv::Size2d(25.0, 25.0);
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
    cv::Rect leftRoi = cv::Rect(350, 0, 1900, 2048);
    cv::Rect rightRoi = cv::Rect(900, 0, 1500, 2048);
    int grayThreshold = 120;
    int minGray = 20;
    LaserColor laserColor = LaserColor::Gray;
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
    double matchDistanceThreshold = 0.5;
};

// 单帧左右图像重建结果。
// 单帧重建诊断信息，用于判断问题发生在提线、匹配还是三维恢复阶段。
struct FrameReconstructionDiagnostics {
    int leftLinePointCount = 0;      // 左图提取到的激光中心线点数。
    int rightLinePointCount = 0;     // 右图提取到的激光中心线点数。
    double leftLineCoverage = 0.0;   // 左中心线在 ROI 内的覆盖比例，越低越可能是 ROI 或阈值问题。
    double rightLineCoverage = 0.0;  // 右中心线在 ROI 内的覆盖比例，越低越可能是 ROI 或阈值问题。
    int matchedPointCount = 0;       // 通过双目几何阈值筛选的匹配点数。
    double matchRate = 0.0;          // 匹配点数 / 左右较少中心线点数，用于判断匹配阈值是否过严。
    double meanMatchError = 0.0;     // 通过阈值的匹配误差均值，用于观察匹配质量。
    double maxMatchError = 0.0;      // 通过阈值的最大匹配误差，用于发现局部异常匹配。
    bool hasPointBounds = false;     // 是否存在可用三维点包围盒。
    Eigen::Vector3d minPoint = Eigen::Vector3d::Zero(); // 当前帧三维点最小坐标。
    Eigen::Vector3d maxPoint = Eigen::Vector3d::Zero(); // 当前帧三维点最大坐标。
    std::string failureReason = "ok"; // ok/left_empty/right_empty/both_empty/match_empty/points_empty。
};

struct FrameReconstructionResult {
    std::string leftImagePath;
    std::string rightImagePath;
    std::vector<Eigen::Vector3d> points;
    cv::Mat leftLinePreview;
    cv::Mat rightLinePreview;
    FrameReconstructionDiagnostics diagnostics;
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
    double matchDistanceThreshold = 0.5;
};

} // namespace htmsr
