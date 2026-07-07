// 文件说明：
// 使用双目基线和射线几何关系匹配左右中心线点。

#include "reconstruction_core/epipolar_line_matcher.h"

#include <limits>
#include <unordered_set>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <opencv2/calib3d.hpp>
#include <opencv2/core/eigen.hpp>
#include <opencv2/imgproc.hpp>

namespace htmsr::reconstruction_core {

namespace {

Eigen::Vector3d pixelToDirection(const cv::Point2f& pixel, const Eigen::Matrix3d& intrinsic) {
    return Eigen::Vector3d(
        (pixel.x - intrinsic(0, 2)) / intrinsic(0, 0),
        (pixel.y - intrinsic(1, 2)) / intrinsic(1, 1),
        1.0);
}

cv::Mat createSideBySidePreview(const cv::Mat& leftImage, const cv::Mat& rightImage) {
    cv::Mat leftBgr;
    cv::Mat rightBgr;
    if (leftImage.channels() == 1) {
        cv::cvtColor(leftImage, leftBgr, cv::COLOR_GRAY2BGR);
    } else {
        leftBgr = leftImage.clone();
    }
    if (rightImage.channels() == 1) {
        cv::cvtColor(rightImage, rightBgr, cv::COLOR_GRAY2BGR);
    } else {
        rightBgr = rightImage.clone();
    }

    const int canvasHeight = std::max(leftBgr.rows, rightBgr.rows);
    const int canvasWidth = leftBgr.cols + rightBgr.cols;
    cv::Mat preview(canvasHeight, canvasWidth, CV_8UC3, cv::Scalar(24, 24, 24));
    leftBgr.copyTo(preview(cv::Rect(0, 0, leftBgr.cols, leftBgr.rows)));
    rightBgr.copyTo(preview(cv::Rect(leftBgr.cols, 0, rightBgr.cols, rightBgr.rows)));
    return preview;
}

}  // namespace

StereoMatchingOutput EpipolarLineMatcher::match(
    const cv::Mat& leftImage,
    const cv::Mat& rightImage,
    const data_model::StereoFrameManifestRow&,
    const data_model::ReconstructionConfig& config,
    const StereoCalibrationData& calibration,
    const algorithms::CenterlineExtractionOutput& leftCenterline,
    const algorithms::CenterlineExtractionOutput& rightCenterline) const {
    StereoMatchingOutput output;

    // 先准备一张左右拼接的调试图，用于展示匹配连线。

    output.previewImage = createSideBySidePreview(leftImage, rightImage);

    if (!calibration.valid) {
        output.diagnosticMessage = "双目标定尚未加载";
        return output;
    }
    if (leftCenterline.points.empty() || rightCenterline.points.empty()) {
        output.diagnosticMessage = "左右中心线点不足，无法匹配";
        return output;
    }

    // 将自定义点类型转换为 OpenCV 点类型，便于去畸变。

    std::vector<cv::Point2f> leftPixels;
    std::vector<cv::Point2f> rightPixels;
    leftPixels.reserve(leftCenterline.points.size());
    rightPixels.reserve(rightCenterline.points.size());
    for (const data_model::Point2D& point : leftCenterline.points) {
        leftPixels.emplace_back(point.x, point.y);
    }
    for (const data_model::Point2D& point : rightCenterline.points) {
        rightPixels.emplace_back(point.x, point.y);
    }

    // 使用双目标定参数对左右中心线点做去畸变。

    std::vector<cv::Point2f> leftUndistorted;
    std::vector<cv::Point2f> rightUndistorted;
    cv::undistortPoints(leftPixels, leftUndistorted, calibration.K1, calibration.D1, cv::noArray(), calibration.P1.empty() ? calibration.K1 : calibration.P1);
    cv::undistortPoints(rightPixels, rightUndistorted, calibration.K2, calibration.D2, cv::noArray(), calibration.P2.empty() ? calibration.K2 : calibration.P2);

    Eigen::Matrix3d leftIntrinsic;
    Eigen::Matrix3d rightIntrinsic;
    cv::cv2eigen(calibration.K1, leftIntrinsic);
    cv::cv2eigen(calibration.K2, rightIntrinsic);

    Eigen::Matrix3d rotation;
    cv::cv2eigen(calibration.R, rotation);
    const Eigen::Vector3d translation(calibration.t.at<double>(0), calibration.t.at<double>(1), calibration.t.at<double>(2));
    const double baselineNorm = translation.norm();
    if (baselineNorm <= 0.0) {
        output.diagnosticMessage = "双目标定基线无效";
        return output;
    }
    const Eigen::Vector3d baselineDirection = translation / baselineNorm;

    // 以点数更少的一侧为主循环，降低匹配开销。

    const bool iterateLeftFirst = leftUndistorted.size() <= rightUndistorted.size();
    std::unordered_set<int> usedIndices;
    const auto matchOneSide = [&](bool leftFirst) {
        if (leftFirst) {
            for (std::size_t i = 0; i < leftUndistorted.size(); ++i) {
                const Eigen::Vector3d leftRay = pixelToDirection(leftUndistorted[i], leftIntrinsic).normalized();
                const Eigen::Vector3d leftRayInRight = rotation * leftRay;

                double bestError = std::numeric_limits<double>::max();
                int bestIndex = -1;
                for (std::size_t j = 0; j < rightUndistorted.size(); ++j) {
                    if (usedIndices.find(static_cast<int>(j)) != usedIndices.end()) {
                        continue;
                    }
                    const Eigen::Vector3d rightRay = pixelToDirection(rightUndistorted[j], rightIntrinsic).normalized();
                    const double error = std::abs((leftRayInRight.cross(rightRay)).dot(baselineDirection));
                    if (error < bestError) {
                        bestError = error;
                        bestIndex = static_cast<int>(j);
                    }
                }

                // 仅接受误差低于阈值的匹配对。

                if (bestIndex >= 0 && bestError < config.matchingDistance) {
                    usedIndices.insert(bestIndex);
                    output.pairs.push_back({
                        {leftPixels[i].x, leftPixels[i].y},
                        {rightPixels[static_cast<std::size_t>(bestIndex)].x, rightPixels[static_cast<std::size_t>(bestIndex)].y},
                        bestError});
                }
            }
        } else {
            for (std::size_t i = 0; i < rightUndistorted.size(); ++i) {
                const Eigen::Vector3d rightRay = pixelToDirection(rightUndistorted[i], rightIntrinsic).normalized();

                double bestError = std::numeric_limits<double>::max();
                int bestIndex = -1;
                for (std::size_t j = 0; j < leftUndistorted.size(); ++j) {
                    if (usedIndices.find(static_cast<int>(j)) != usedIndices.end()) {
                        continue;
                    }
                    const Eigen::Vector3d leftRay = pixelToDirection(leftUndistorted[j], leftIntrinsic).normalized();
                    const Eigen::Vector3d leftRayInRight = rotation * leftRay;
                    const double error = std::abs((leftRayInRight.cross(rightRay)).dot(baselineDirection));
                    if (error < bestError) {
                        bestError = error;
                        bestIndex = static_cast<int>(j);
                    }
                }

                if (bestIndex >= 0 && bestError < config.matchingDistance) {
                    usedIndices.insert(bestIndex);
                    output.pairs.push_back({
                        {leftPixels[static_cast<std::size_t>(bestIndex)].x, leftPixels[static_cast<std::size_t>(bestIndex)].y},
                        {rightPixels[i].x, rightPixels[i].y},
                        bestError});
                }
            }
        }
    };

    matchOneSide(iterateLeftFirst);

    // 将匹配结果绘制到拼接调试图上。

    const int rightOffsetX = leftImage.cols;
    for (const data_model::StereoPointMatch& pair : output.pairs) {
        const cv::Point leftPoint(static_cast<int>(pair.leftPoint.x), static_cast<int>(pair.leftPoint.y));
        const cv::Point rightPoint(static_cast<int>(pair.rightPoint.x) + rightOffsetX, static_cast<int>(pair.rightPoint.y));
        cv::circle(output.previewImage, leftPoint, 2, cv::Scalar(0, 255, 0), -1);
        cv::circle(output.previewImage, rightPoint, 2, cv::Scalar(0, 255, 255), -1);
        cv::line(output.previewImage, leftPoint, rightPoint, cv::Scalar(255, 128, 0), 1);
    }

    output.diagnosticMessage = QString("双目匹配得到 %1 对点").arg(output.pairs.size());
    return output;
}

}  // namespace htmsr::reconstruction_core
