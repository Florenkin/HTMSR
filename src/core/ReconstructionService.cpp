#include "core/ReconstructionService.h"

#include "core/FileSystemUtils.h"
#include "core/Logger.h"

#include <Eigen/Cholesky>
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <opencv2/calib3d.hpp>
#include <opencv2/core/eigen.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace htmsr {
namespace {

double computeLineCoverage(const std::vector<Eigen::Vector2d>& points, const cv::Rect& roi)
{
    if (points.empty() || roi.width <= 0 || roi.height <= 0) {
        return 0.0;
    }

    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    for (const auto& point : points) {
        minX = std::min(minX, point.x());
        maxX = std::max(maxX, point.x());
        minY = std::min(minY, point.y());
        maxY = std::max(maxY, point.y());
    }

    // 暂时不依赖横向/纵向提线参数，取 x/y 两个方向中覆盖更大的比例。
    const double xCoverage = (maxX - minX + 1.0) / static_cast<double>(roi.width);
    const double yCoverage = (maxY - minY + 1.0) / static_cast<double>(roi.height);
    return std::clamp(std::max(xCoverage, yCoverage), 0.0, 1.0);
}

void computePointBounds(const std::vector<Eigen::Vector3d>& points, FrameReconstructionDiagnostics& diagnostics)
{
    if (points.empty()) {
        diagnostics.hasPointBounds = false;
        return;
    }

    diagnostics.hasPointBounds = true;
    diagnostics.minPoint = points.front();
    diagnostics.maxPoint = points.front();
    for (const auto& point : points) {
        diagnostics.minPoint = diagnostics.minPoint.cwiseMin(point);
        diagnostics.maxPoint = diagnostics.maxPoint.cwiseMax(point);
    }
}

std::string formatDouble(double value, int precision = 3)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::string formatPoint(const Eigen::Vector3d& point)
{
    return formatDouble(point.x()) + "," + formatDouble(point.y()) + "," + formatDouble(point.z());
}

std::string formatBounds(const FrameReconstructionDiagnostics& diagnostics)
{
    if (!diagnostics.hasPointBounds) {
        return "none";
    }
    return "[" + formatPoint(diagnostics.minPoint) + "]-[" + formatPoint(diagnostics.maxPoint) + "]";
}

std::string formatFrameDiagnostics(int frameIndex, const FrameReconstructionResult& frame)
{
    const auto& diagnostics = frame.diagnostics;
    std::ostringstream stream;
    stream << "Frame " << frameIndex
           << ": leftLine=" << diagnostics.leftLinePointCount
           << " leftCov=" << formatDouble(diagnostics.leftLineCoverage, 2)
           << " rightLine=" << diagnostics.rightLinePointCount
           << " rightCov=" << formatDouble(diagnostics.rightLineCoverage, 2)
           << " matched=" << diagnostics.matchedPointCount
           << " matchRate=" << formatDouble(diagnostics.matchRate, 2)
           << " matchErrMean=" << formatDouble(diagnostics.meanMatchError, 3)
           << " matchErrMax=" << formatDouble(diagnostics.maxMatchError, 3)
           << " points=" << frame.points.size()
           << " bounds=" << formatBounds(diagnostics)
           << " reason=" << diagnostics.failureReason;
    return stream.str();
}

bool shouldLogFrameDiagnostics(int frameIndex, int frameCount, const FrameReconstructionResult& frame)
{
    if (frame.diagnostics.failureReason != "ok") {
        return true;
    }
    if (frameCount <= 200) {
        return true;
    }
    return frameIndex <= 10 || frameIndex == frameCount || frameIndex % 50 == 0;
}

} // namespace

ReconstructionResult ReconstructionService::reconstruct(const ReconstructionInput& input, CancellationToken cancellation,
    const FramePreviewCallback& previewCallback) const
{
    cancellation.check();
    if (!input.calibration.isValid()) {
        throw std::runtime_error("Reconstruction requires a valid calibration result.");
    }

    Logger::instance().info("Reconstruction", "Starting batch reconstruction.");

    // 读取左右重建图像路径，按排序结果一一配对。
    const auto leftPaths = listImageFiles(input.leftDirectory, input.imageRange);
    const auto rightPaths = listImageFiles(input.rightDirectory, input.imageRange);
    const int pairCount = static_cast<int>(std::min(leftPaths.size(), rightPaths.size()));
    if (pairCount == 0) {
        throw std::runtime_error("No reconstruction image pairs were found.");
    }
    if (leftPaths.size() != rightPaths.size()) {
        Logger::instance().warning("Reconstruction", "Left and right reconstruction image counts differ. Using paired minimum count.");
    }

    ReconstructionResult result;
    result.frames.reserve(pairCount);

    // 对每一对左右图像独立执行一次三维重建，点云直接追加到总结果，避免大批量帧时重复占用内存。
    for (int i = 0; i < pairCount; ++i) {
        cancellation.check();
        try {
            cv::Mat left = cv::imread(leftPaths[i], cv::IMREAD_COLOR);
            cv::Mat right = cv::imread(rightPaths[i], cv::IMREAD_COLOR);
            if (left.empty() || right.empty()) {
                Logger::instance().warning("Reconstruction", "Skipping unreadable image pair index " + std::to_string(i));
                continue;
            }

            FrameReconstructionResult frame;
            frame.leftImagePath = leftPaths[i];
            frame.rightImagePath = rightPaths[i];
            frame.points = reconstructFrame(
                left,
                right,
                input.calibration,
                input.laserConfig,
                input.matchDistanceThreshold,
                frame.leftLinePreview,
                frame.rightLinePreview,
                frame.diagnostics,
                i,
                previewCallback);

            if (shouldLogFrameDiagnostics(i + 1, pairCount, frame)) {
                Logger::instance().debug("Reconstruction", formatFrameDiagnostics(i + 1, frame));
            }
            result.mergedPoints.insert(result.mergedPoints.end(), frame.points.begin(), frame.points.end());
            const int progressInterval = std::max(1, pairCount / 10 + (pairCount % 10 != 0));
            if (i == 0 || i + 1 == pairCount || (i + 1) % progressInterval == 0) {
                Logger::instance().info("Reconstruction", "重建进度：" + std::to_string(i + 1) +
                    "/" + std::to_string(pairCount) + " 组，累计点数：" + std::to_string(result.mergedPoints.size()));
            }
            frame.points.clear();
            frame.points.shrink_to_fit();
            frame.leftLinePreview.release();
            frame.rightLinePreview.release();
            result.frames.push_back(std::move(frame));
        } catch (const cv::Exception& ex) {
            Logger::instance().warning(
                "Reconstruction",
                "Skipping reconstruction image pair index " + std::to_string(i) + " because OpenCV reported: " + ex.what());
        } catch (const std::exception& ex) {
            Logger::instance().warning(
                "Reconstruction",
                "Skipping reconstruction image pair index " + std::to_string(i) + " because: " + ex.what());
        }
    }

    cancellation.check();
    result.success = !result.mergedPoints.empty();
    result.message = result.success
        ? "Batch reconstruction finished. Total points=" + std::to_string(result.mergedPoints.size())
        : "Reconstruction finished without valid points. Check calibration, ROI, laser threshold, and captured image quality.";
    Logger::instance().log(result.success ? LogLevel::Info : LogLevel::Warning, "Reconstruction", result.message);
    return result;
}

std::vector<Eigen::Vector3d> ReconstructionService::reconstructFrame(
    const cv::Mat& leftImage,
    const cv::Mat& rightImage,
    const CalibrationResult& calibration,
    const LaserExtractionConfig& laserConfig,
    double matchDistanceThreshold,
    cv::Mat& leftPreview,
    cv::Mat& rightPreview,
    FrameReconstructionDiagnostics& diagnostics,
    int frameIndex,
    const FramePreviewCallback& previewCallback) const
{
    diagnostics = FrameReconstructionDiagnostics{};

    // 第一步：分别提取左右图像中的激光中心线。
    LaserExtractionService extractor;
    const auto leftLine = extractor.extract(leftImage, laserConfig.leftRoi, laserConfig);
    const auto rightLine = extractor.extract(rightImage, laserConfig.rightRoi, laserConfig);
    leftPreview = leftLine.preview;
    rightPreview = rightLine.preview;

    // 提线一完成就交给应用层保存；后续匹配为空或发生异常时仍保留诊断图。
    if (previewCallback) {
        try {
            previewCallback(frameIndex, leftPreview, rightPreview);
        } catch (const std::exception& ex) {
            Logger::instance().warning("Reconstruction",
                "Laser preview callback failed for frame " + std::to_string(frameIndex + 1) + ": " + ex.what());
        } catch (...) {
            Logger::instance().warning("Reconstruction",
                "Laser preview callback failed for frame " + std::to_string(frameIndex + 1) + ".");
        }
    }

    const cv::Rect leftSafeRoi = laserConfig.leftRoi & cv::Rect(0, 0, leftImage.cols, leftImage.rows);
    const cv::Rect rightSafeRoi = laserConfig.rightRoi & cv::Rect(0, 0, rightImage.cols, rightImage.rows);
    // 提线统计先于匹配记录，便于区分是 ROI/阈值问题还是后续几何匹配问题。
    diagnostics.leftLinePointCount = static_cast<int>(leftLine.points.size());
    diagnostics.rightLinePointCount = static_cast<int>(rightLine.points.size());
    diagnostics.leftLineCoverage = computeLineCoverage(leftLine.points, leftSafeRoi);
    diagnostics.rightLineCoverage = computeLineCoverage(rightLine.points, rightSafeRoi);

    if (leftLine.points.empty() || rightLine.points.empty()) {
        // 分开记录左右空线，后续看日志就能判断是单侧曝光/ROI 问题还是双侧都没有提到线。
        if (leftLine.points.empty() && rightLine.points.empty()) {
            diagnostics.failureReason = "both_empty";
        } else if (leftLine.points.empty()) {
            diagnostics.failureReason = "left_empty";
        } else {
            diagnostics.failureReason = "right_empty";
        }
        return {};
    }

    // 将 Eigen 二维点转换为 OpenCV 点，便于调用 undistortPoints。
    std::vector<cv::Point2d> leftPoints;
    std::vector<cv::Point2d> rightPoints;
    leftPoints.reserve(leftLine.points.size());
    rightPoints.reserve(rightLine.points.size());

    for (const auto& point : leftLine.points) {
        leftPoints.emplace_back(point.x(), point.y());
    }
    for (const auto& point : rightLine.points) {
        rightPoints.emplace_back(point.x(), point.y());
    }

    // 第二步：根据左右相机内参和畸变参数，对中心线像素点去畸变。
    std::vector<cv::Point2d> undistortedLeft;
    std::vector<cv::Point2d> undistortedRight;
    cv::undistortPoints(leftPoints, undistortedLeft, calibration.K1, calibration.D1, cv::noArray(), calibration.P1);
    cv::undistortPoints(rightPoints, undistortedRight, calibration.K2, calibration.D2, cv::noArray(), calibration.P2);

    cv::Mat k1Mat;
    cv::Mat k2Mat;
    cv::Mat rMat;
    cv::Mat tMat;
    // OpenCV 矩阵转为 double 类型，再转换到 Eigen 参与几何计算。
    calibration.K1.convertTo(k1Mat, CV_64F);
    calibration.K2.convertTo(k2Mat, CV_64F);
    calibration.R.convertTo(rMat, CV_64F);
    calibration.t.convertTo(tMat, CV_64F);

    Eigen::Matrix3d K1;
    Eigen::Matrix3d K2;
    Eigen::Matrix3d R;
    cv::cv2eigen(k1Mat, K1);
    cv::cv2eigen(k2Mat, K2);
    cv::cv2eigen(rMat, R);

    Eigen::Vector3d t(tMat.at<double>(0), tMat.at<double>(1), tMat.at<double>(2));
    Eigen::Vector3d baseline = t.normalized();

    std::vector<Eigen::Vector2d> matchedLeft;
    std::vector<Eigen::Vector2d> matchedRight;
    std::vector<double> matchErrors;

    // 第三步：在左右中心线上寻找满足双目几何约束的匹配点。
    if (undistortedLeft.size() <= undistortedRight.size()) {
        for (const auto& leftPoint : undistortedLeft) {
            // 左相机射线通过 R 转到右相机坐标系，再与右相机射线比较。
            Eigen::Vector3d leftRay = pixelToRay(Eigen::Vector2d(leftPoint.x, leftPoint.y), K1).normalized();
            Eigen::Vector3d leftRayInRight = R * leftRay;

            double bestError = std::numeric_limits<double>::max();
            size_t bestIndex = 0;
            for (size_t j = 0; j < undistortedRight.size(); ++j) {
                Eigen::Vector3d rightRay = pixelToRay(Eigen::Vector2d(undistortedRight[j].x, undistortedRight[j].y), K2).normalized();
                // 误差越小，说明两条射线越符合基线方向上的极线约束。
                const double error = std::abs(leftRayInRight.cross(rightRay).dot(baseline));
                if (error < bestError) {
                    bestError = error;
                    bestIndex = j;
                }
            }

            if (bestError < matchDistanceThreshold) {
                matchedLeft.emplace_back(leftPoint.x, leftPoint.y);
                matchedRight.emplace_back(undistortedRight[bestIndex].x, undistortedRight[bestIndex].y);
                matchErrors.push_back(bestError);
            }
        }
    } else {
        for (const auto& rightPoint : undistortedRight) {
            Eigen::Vector3d rightRay = pixelToRay(Eigen::Vector2d(rightPoint.x, rightPoint.y), K2).normalized();

            double bestError = std::numeric_limits<double>::max();
            size_t bestIndex = 0;
            for (size_t j = 0; j < undistortedLeft.size(); ++j) {
                Eigen::Vector3d leftRay = pixelToRay(Eigen::Vector2d(undistortedLeft[j].x, undistortedLeft[j].y), K1).normalized();
                Eigen::Vector3d leftRayInRight = R * leftRay;
                // 当右图点数量更少时，从右图点反向寻找最匹配的左图点。
                const double error = std::abs(leftRayInRight.cross(rightRay).dot(baseline));
                if (error < bestError) {
                    bestError = error;
                    bestIndex = j;
                }
            }

            if (bestError < matchDistanceThreshold) {
                matchedLeft.emplace_back(undistortedLeft[bestIndex].x, undistortedLeft[bestIndex].y);
                matchedRight.emplace_back(rightPoint.x, rightPoint.y);
                matchErrors.push_back(bestError);
            }
        }
    }

    diagnostics.matchedPointCount = static_cast<int>(matchedLeft.size());
    const size_t matchDenominator = std::min(undistortedLeft.size(), undistortedRight.size());
    diagnostics.matchRate = matchDenominator == 0
        ? 0.0
        : static_cast<double>(diagnostics.matchedPointCount) / static_cast<double>(matchDenominator);
    if (!matchErrors.empty()) {
        // 匹配误差能直接反映阈值是否过严或过松，是后续调参的主要依据。
        diagnostics.meanMatchError = std::accumulate(matchErrors.begin(), matchErrors.end(), 0.0) / static_cast<double>(matchErrors.size());
        diagnostics.maxMatchError = *std::max_element(matchErrors.begin(), matchErrors.end());
    }

    if (matchedLeft.empty()) {
        diagnostics.failureReason = "match_empty";
        return {};
    }

    // 第四步：对每一组匹配点构造空间射线，并恢复三维点。
    std::vector<Eigen::Vector3d> reconstructed;
    reconstructed.reserve(matchedLeft.size());
    for (size_t i = 0; i < matchedLeft.size(); ++i) {
        Eigen::Vector3d leftRay = pixelToRay(matchedLeft[i], K1).normalized();
        Eigen::Vector3d leftRayInRight = R * leftRay;
        Eigen::Vector3d rightRay = pixelToRay(matchedRight[i], K2).normalized();

        // 在右相机坐标系中求两条射线的最近点中点，再转换回左相机坐标系。
        const Eigen::Vector3d pointInRight = closestPointBetweenLines(leftRayInRight, t, rightRay, Eigen::Vector3d::Zero());
        const Eigen::Vector3d pointInLeft = R.inverse() * (pointInRight - t);
        reconstructed.push_back(pointInLeft);
    }

    if (reconstructed.empty()) {
        diagnostics.failureReason = "points_empty";
        return reconstructed;
    }

    // 点云包围盒用于快速发现深度发散、尺度异常或局部飞点。
    computePointBounds(reconstructed, diagnostics);
    diagnostics.failureReason = "ok";
    return reconstructed;
}

Eigen::Vector3d ReconstructionService::pixelToRay(const Eigen::Vector2d& pixel, const Eigen::Matrix3d& cameraMatrix) const
{
    // 使用针孔模型反投影：x=(u-cx)/fx，y=(v-cy)/fy，z=1。
    return Eigen::Vector3d(
        (pixel.x() - cameraMatrix(0, 2)) / cameraMatrix(0, 0),
        (pixel.y() - cameraMatrix(1, 2)) / cameraMatrix(1, 1),
        1.0);
}

Eigen::Vector3d ReconstructionService::closestPointBetweenLines(
    const Eigen::Vector3d& directionA,
    const Eigen::Vector3d& pointA,
    const Eigen::Vector3d& directionB,
    const Eigen::Vector3d& pointB) const
{
    // 最小二乘求解两条空间直线的最近点参数。
    Eigen::Matrix<double, 3, 2> A;
    A.col(0) = directionA;
    A.col(1) = -directionB;

    const Eigen::Vector2d coeff = (A.transpose() * A).ldlt().solve(A.transpose() * (pointB - pointA));
    // 由于噪声影响两条射线通常不严格相交，取两最近点的中点作为重建点。
    const Eigen::Vector3d closestA = pointA + coeff.x() * directionA;
    const Eigen::Vector3d closestB = pointB + coeff.y() * directionB;
    return 0.5 * (closestA + closestB);
}

} // namespace htmsr
