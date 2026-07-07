// 文件说明：
// 兼容多种字段命名方式读取双目标定 YAML 文件。

#include "reconstruction_core/stereo_calibration_loader.h"

#include <array>

#include <opencv2/core/persistence.hpp>

namespace htmsr::reconstruction_core {

namespace {

template <typename T, std::size_t N>
bool readFirstAvailable(cv::FileStorage& storage, const std::array<const char*, N>& keys, T& value) {
    for (const char* key : keys) {
        cv::FileNode node = storage[key];
        if (!node.empty()) {
            node >> value;
            return true;
        }
    }
    return false;
}

cv::Mat ensureColumnVector(const cv::Mat& matrix) {
    if (matrix.empty()) {
        return matrix;
    }
    if (matrix.rows == 3 && matrix.cols == 1) {
        return matrix.clone();
    }
    if (matrix.rows == 1 && matrix.cols == 3) {
        cv::Mat transposed;
        cv::transpose(matrix, transposed);
        return transposed;
    }
    return matrix.clone();
}

cv::Size readImageSize(cv::FileStorage& storage) {
    cv::Size size;
    if (readFirstAvailable(storage, std::array<const char*, 4>{"imageSize", "picSize", "leftImageSize", "ImageSize"}, size)) {
        return size;
    }

    cv::Mat sizeMat;
    if (readFirstAvailable(storage, std::array<const char*, 2>{"image_size", "pictureSize"}, sizeMat) && sizeMat.total() >= 2) {
        size.width = static_cast<int>(sizeMat.at<int>(0));
        size.height = static_cast<int>(sizeMat.at<int>(1));
    }
    return size;
}

}  // namespace

bool StereoCalibrationLoader::load(const QString& filePath, StereoCalibrationData& calibrationData, QString& error) const {
    cv::FileStorage storage(filePath.toStdString(), cv::FileStorage::READ);
    if (!storage.isOpened()) {
        error = QString("无法打开双目标定文件: %1").arg(filePath);
        return false;
    }

    calibrationData = {};
    calibrationData.sourcePath = filePath;

    // 兼容不同项目中的字段命名，尽量从已有双目标定文件恢复关键参数。

    readFirstAvailable(storage, std::array<const char*, 5>{"K1", "CameraMatrix1", "cameraMatrix1", "M1", "LeftCameraMatrix"}, calibrationData.K1);
    readFirstAvailable(storage, std::array<const char*, 5>{"D1", "DistCoeffs1", "distCoeffs1", "dist1", "LeftDistCoeffs"}, calibrationData.D1);
    readFirstAvailable(storage, std::array<const char*, 5>{"K2", "CameraMatrix2", "cameraMatrix2", "M2", "RightCameraMatrix"}, calibrationData.K2);
    readFirstAvailable(storage, std::array<const char*, 5>{"D2", "DistCoeffs2", "distCoeffs2", "dist2", "RightDistCoeffs"}, calibrationData.D2);
    readFirstAvailable(storage, std::array<const char*, 4>{"R", "Rotation", "R12", "stereo_R"}, calibrationData.R);
    readFirstAvailable(storage, std::array<const char*, 4>{"t", "T", "Translation", "stereo_t"}, calibrationData.t);
    readFirstAvailable(storage, std::array<const char*, 3>{"E", "Essential", "stereo_E"}, calibrationData.E);
    readFirstAvailable(storage, std::array<const char*, 3>{"F", "Fundamental", "stereo_F"}, calibrationData.F);
    readFirstAvailable(storage, std::array<const char*, 2>{"P1", "Projection1"}, calibrationData.P1);
    readFirstAvailable(storage, std::array<const char*, 2>{"P2", "Projection2"}, calibrationData.P2);

    calibrationData.leftImageSize = readImageSize(storage);
    calibrationData.rightImageSize = calibrationData.leftImageSize;
    calibrationData.t = ensureColumnVector(calibrationData.t);

    // 若投影矩阵未提供，则退回使用内参矩阵。

    if (calibrationData.P1.empty() && !calibrationData.K1.empty()) {
        calibrationData.P1 = calibrationData.K1.clone();
    }
    if (calibrationData.P2.empty() && !calibrationData.K2.empty()) {
        calibrationData.P2 = calibrationData.K2.clone();
    }

    if (calibrationData.K1.empty() || calibrationData.D1.empty() || calibrationData.K2.empty() || calibrationData.D2.empty() ||
        calibrationData.R.empty() || calibrationData.t.empty()) {
        error = QString("双目标定文件缺少关键字段(K1/D1/K2/D2/R/t): %1").arg(filePath);
        return false;
    }

    calibrationData.valid = true;
    return true;
}

}  // namespace htmsr::reconstruction_core
