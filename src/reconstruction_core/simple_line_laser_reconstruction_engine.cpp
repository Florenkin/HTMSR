// 文件说明：
// 旧单目线激光重建实现，当前保留但不作为主链路使用。

#include "reconstruction_core/simple_line_laser_reconstruction_engine.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace htmsr::reconstruction_core {

ReconstructionFrameOutput SimpleLineLaserReconstructionEngine::reconstruct(
    const cv::Mat& image,
    const data_model::FrameManifestRow& manifestRow,
    const data_model::ReconstructionConfig&,
    const UuwCalibrationData& calibration,
    const algorithms::CenterlineExtractionOutput& centerline) const {
    ReconstructionFrameOutput output;
    output.previewImage = centerline.previewImage.clone();

    if (image.empty()) {
        output.message = "输入图像为空";
        return output;
    }
    if (!calibration.valid) {
        output.message = "标定尚未加载";
        return output;
    }

    std::vector<cv::Point2f> pixels;
    pixels.reserve(centerline.points.size());
    for (const data_model::Point2D& point : centerline.points) {
        pixels.emplace_back(point.x, point.y);
    }

    std::vector<cv::Point2f> undistorted;
    if (!pixels.empty()) {
        cv::undistortPoints(pixels, undistorted, calibration.cameraMatrix, calibration.distCoeffs, cv::noArray(), calibration.cameraMatrix);
    }

    output.points.reserve(undistorted.size());
    const float zBase = static_cast<float>(manifestRow.laserPosition);
    const float xCenter = static_cast<float>(calibration.pictureSize.width > 0 ? calibration.pictureSize.width / 2.0 : image.cols / 2.0);
    const float yCenter = static_cast<float>(calibration.pictureSize.height > 0 ? calibration.pictureSize.height / 2.0 : image.rows / 2.0);
    for (const cv::Point2f& point : undistorted) {
        const float x = (point.x - xCenter) * 0.05f;
        const float y = (point.y - yCenter) * 0.05f;
        const float z = zBase;
        output.points.emplace_back(x, y, z);
    }

    output.message = QString("重建得到 %1 个三维点").arg(output.points.size());
    return output;
}

}  // namespace htmsr::reconstruction_core
