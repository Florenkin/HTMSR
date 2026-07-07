// 文件说明：
// 将双目匹配结果恢复为三维点云。

#include "reconstruction_core/stereo_line_laser_reconstruction_engine.h"

#include <Eigen/Core>
#include <Eigen/LU>
#include <Eigen/QR>

#include <opencv2/calib3d.hpp>
#include <opencv2/core/eigen.hpp>

namespace htmsr::reconstruction_core {

namespace {

Eigen::Vector3d pixelToDirection(const cv::Point2f& pixel, const Eigen::Matrix3d& intrinsic) {
    return Eigen::Vector3d(
        (pixel.x - intrinsic(0, 2)) / intrinsic(0, 0),
        (pixel.y - intrinsic(1, 2)) / intrinsic(1, 1),
        1.0);
}

Eigen::Vector3d midpointOfClosestPoints(
    const Eigen::Vector3d& line1Origin,
    const Eigen::Vector3d& line1Direction,
    const Eigen::Vector3d& line2Origin,
    const Eigen::Vector3d& line2Direction) {
    Eigen::Matrix<double, 3, 2> matrix;
    matrix << line1Direction(0), -line2Direction(0),
        line1Direction(1), -line2Direction(1),
        line1Direction(2), -line2Direction(2);
    const Eigen::Vector3d rhs = line2Origin - line1Origin;
    const Eigen::Vector2d parameters = matrix.colPivHouseholderQr().solve(rhs);
    const Eigen::Vector3d point1 = line1Origin + line1Direction * parameters(0);
    const Eigen::Vector3d point2 = line2Origin + line2Direction * parameters(1);
    return (point1 + point2) * 0.5;
}

}  // namespace

ReconstructionFrameOutput StereoLineLaserReconstructionEngine::reconstruct(
    const cv::Mat& leftImage,
    const cv::Mat& rightImage,
    const data_model::StereoFrameManifestRow&,
    const data_model::ReconstructionConfig&,
    const StereoCalibrationData& calibration,
    const StereoMatchingOutput& matches) const {
    ReconstructionFrameOutput output;

    // 复用匹配阶段生成的调试图作为当前帧预览图。

    output.previewImage = matches.previewImage.clone();

    if (leftImage.empty() || rightImage.empty()) {
        output.message = "左右输入图像为空";
        return output;
    }
    if (!calibration.valid) {
        output.message = "双目标定尚未加载";
        return output;
    }
    if (matches.pairs.empty()) {
        output.message = "没有可用于三维重建的匹配点";
        return output;
    }

    // 将 OpenCV 标定矩阵转换为 Eigen，方便进行射线几何计算。

    Eigen::Matrix3d leftIntrinsic;
    Eigen::Matrix3d rightIntrinsic;
    Eigen::Matrix3d rotation;
    cv::cv2eigen(calibration.K1, leftIntrinsic);
    cv::cv2eigen(calibration.K2, rightIntrinsic);
    cv::cv2eigen(calibration.R, rotation);
    const Eigen::Vector3d translation(calibration.t.at<double>(0), calibration.t.at<double>(1), calibration.t.at<double>(2));

    std::vector<cv::Point2f> leftPixels;
    std::vector<cv::Point2f> rightPixels;
    leftPixels.reserve(matches.pairs.size());
    rightPixels.reserve(matches.pairs.size());
    for (const data_model::StereoPointMatch& pair : matches.pairs) {
        leftPixels.emplace_back(pair.leftPoint.x, pair.leftPoint.y);
        rightPixels.emplace_back(pair.rightPoint.x, pair.rightPoint.y);
    }

    // 对最终参与重建的匹配点再次做去畸变，保证几何求交使用的是归一化射线。

    std::vector<cv::Point2f> leftUndistorted;
    std::vector<cv::Point2f> rightUndistorted;
    cv::undistortPoints(leftPixels, leftUndistorted, calibration.K1, calibration.D1, cv::noArray(), calibration.P1.empty() ? calibration.K1 : calibration.P1);
    cv::undistortPoints(rightPixels, rightUndistorted, calibration.K2, calibration.D2, cv::noArray(), calibration.P2.empty() ? calibration.K2 : calibration.P2);

    // 使用两条空间射线最近点中点法恢复三维坐标。

    output.points.reserve(matches.pairs.size());
    for (std::size_t i = 0; i < leftUndistorted.size() && i < rightUndistorted.size(); ++i) {
        const Eigen::Vector3d leftRay = pixelToDirection(leftUndistorted[i], leftIntrinsic).normalized();
        const Eigen::Vector3d rightRay = pixelToDirection(rightUndistorted[i], rightIntrinsic).normalized();

        const Eigen::Vector3d leftRayInRight = rotation * leftRay;
        const Eigen::Vector3d pointInRight = midpointOfClosestPoints(
            translation,
            leftRayInRight,
            Eigen::Vector3d::Zero(),
            rightRay);
        const Eigen::Vector3d pointInLeft = rotation.inverse() * (pointInRight - translation);

        if (!pointInLeft.allFinite()) {
            continue;
        }

        output.points.emplace_back(
            static_cast<float>(pointInLeft.x()),
            static_cast<float>(pointInLeft.y()),
            static_cast<float>(pointInLeft.z()));
    }

    output.message = QString("双目三维重建得到 %1 个点").arg(output.points.size());
    return output;
}

}  // namespace htmsr::reconstruction_core
