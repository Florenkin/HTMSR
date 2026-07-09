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
#include <limits>
#include <stdexcept>

namespace htmsr {

ReconstructionResult ReconstructionService::reconstruct(const ReconstructionInput& input) const
{
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

    // 对每一对左右图像独立执行一次三维重建。
    for (int i = 0; i < pairCount; ++i) {
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
            frame.rightLinePreview);

        Logger::instance().info("Reconstruction",
            "Frame " + std::to_string(i + 1) + " reconstructed points=" + std::to_string(frame.points.size()));
        result.frames.push_back(std::move(frame));
    }

    PointCloudService pointCloudService;
    // 将逐帧点集合并成一个点云，便于 UI 显示和统一导出。
    result.mergedPoints = pointCloudService.mergeFrames(result.frames);
    Logger::instance().info("Reconstruction", "Batch reconstruction finished. Total points=" + std::to_string(result.mergedPoints.size()));
    return result;
}

std::vector<Eigen::Vector3d> ReconstructionService::reconstructFrame(
    const cv::Mat& leftImage,
    const cv::Mat& rightImage,
    const CalibrationResult& calibration,
    const LaserExtractionConfig& laserConfig,
    double matchDistanceThreshold,
    cv::Mat& leftPreview,
    cv::Mat& rightPreview) const
{
    // 第一步：分别提取左右图像中的激光中心线。
    LaserExtractionService extractor;
    const auto leftLine = extractor.extract(leftImage, laserConfig.leftRoi, laserConfig);
    const auto rightLine = extractor.extract(rightImage, laserConfig.rightRoi, laserConfig);
    leftPreview = leftLine.preview;
    rightPreview = rightLine.preview;

    if (leftLine.points.empty() || rightLine.points.empty()) {
        Logger::instance().warning("Reconstruction", "Laser line extraction returned empty points.");
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
            }
        }
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
