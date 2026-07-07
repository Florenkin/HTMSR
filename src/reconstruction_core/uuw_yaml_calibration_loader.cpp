// 文件说明：
// 旧 UUW 标定加载实现，当前保留但不作为主链路使用。

#include "reconstruction_core/uuw_yaml_calibration_loader.h"

#include <opencv2/core/persistence.hpp>

namespace htmsr::reconstruction_core {

bool UuwYamlCalibrationLoader::load(const QString& filePath, UuwCalibrationData& calibrationData, QString& error) const {
    cv::FileStorage storage(filePath.toStdString(), cv::FileStorage::READ);
    if (!storage.isOpened()) {
        error = QString("无法打开标定文件: %1").arg(filePath);
        return false;
    }

    calibrationData = {};
    calibrationData.sourcePath = filePath;

    storage["CameraMatrix"] >> calibrationData.cameraMatrix;
    storage["DistCoeffs"] >> calibrationData.distCoeffs;
    storage["picSize"] >> calibrationData.pictureSize;
    storage["surfBeginPos"] >> calibrationData.surfBeginPos;
    storage["surfEndPos"] >> calibrationData.surfEndPos;
    storage["surfStep"] >> calibrationData.surfStep;
    storage["surfNum"] >> calibrationData.surfNum;

    if (!storage["d0"].empty()) {
        storage["d0"] >> calibrationData.d0;
    }
    if (!storage["d1"].empty()) {
        storage["d1"] >> calibrationData.d1;
    }
    if (!storage["mu"].empty()) {
        cv::Mat mu;
        storage["mu"] >> mu;
        if (mu.total() >= 3) {
            calibrationData.mu = cv::Vec3d(mu.at<double>(0), mu.at<double>(1), mu.at<double>(2));
        }
    }
    if (!storage["A"].empty()) {
        cv::Mat axis;
        storage["A"] >> axis;
        if (axis.total() >= 3) {
            calibrationData.axis = cv::Vec3d(axis.at<double>(0), axis.at<double>(1), axis.at<double>(2));
        }
    }

    if (calibrationData.cameraMatrix.empty()) {
        error = QString("标定文件缺少 CameraMatrix: %1").arg(filePath);
        return false;
    }

    calibrationData.valid = true;
    return true;
}

}  // namespace htmsr::reconstruction_core
