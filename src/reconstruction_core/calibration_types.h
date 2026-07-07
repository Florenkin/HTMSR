// 文件说明：
// 定义双目标定数据结构，保存左右相机和双目外参。

#pragma once

#include <QString>

#include <opencv2/core.hpp>

namespace htmsr::reconstruction_core {

struct StereoCalibrationData {
    cv::Mat K1;
    cv::Mat D1;
    cv::Mat K2;
    cv::Mat D2;
    cv::Mat R;
    cv::Mat t;
    cv::Mat E;
    cv::Mat F;
    cv::Mat P1;
    cv::Mat P2;
    cv::Size leftImageSize;
    cv::Size rightImageSize;
    bool valid = false;
    QString sourcePath;
};

}  // namespace htmsr::reconstruction_core
