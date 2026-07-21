#pragma once

#include "core/Types.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace htmsr {

class CalibrationService {
public:
    /*
        函数功能：根据左右标定图像执行单目标定和双目标定，并可保存标定结果文件
        输入：
            input：双目标定输入参数，包括左右图像目录、棋盘格规格、图像范围和输出文件路径
        输出：
            返回值：双目标定结果，包含左右相机内参、畸变参数、双目外参和误差统计
    */
    CalibrationResult calibrate(const CalibrationInput& input) const;

    /*
        函数功能：从 OpenCV yml/yaml 文件中读取双目标定结果
        输入：
            filename：标定结果文件路径
        输出：
            result：读取到的标定参数
            返回值：关键矩阵完整且有效时返回 true，否则返回 false
    */
    bool loadCalibration(const std::string& filename, CalibrationResult& result) const;

    /*
        函数功能：将双目标定结果保存为 OpenCV yml/yaml 文件
        输入：
            filename：输出标定文件路径
            result：待保存的双目标定结果
        输出：
            无（函数执行后会在磁盘上生成或覆盖对应的标定文件）
    */
    void saveCalibration(const std::string& filename, const CalibrationResult& result) const;

private:
    struct CameraCalibration {
        cv::Mat cameraMatrix;
        cv::Mat distortion;
        std::vector<double> perImageErrors;
        std::vector<int> failedImageNumbers;
        int successCount = 0;
        int failureCount = 0;
    };

    /*
        函数功能：对单台相机执行标定，求解内参、畸变参数并计算每帧重投影误差
        输入：
            images：该相机的一组棋盘格标定图像
            boardSize：棋盘格内角点数量
            squareSize：棋盘格每个小格的实际尺寸
            cameraName：日志中使用的相机名称
        输出：
            返回值：单目标定结果，包括相机内参、畸变参数、误差和成功失败统计
    */
    CameraCalibration calibrateSingleCamera(
        const std::vector<cv::Mat>& images,
        const cv::Size& boardSize,
        const cv::Size2d& squareSize,
        const std::string& cameraName) const;

    /*
        函数功能：根据棋盘格规格生成标定板角点的三维世界坐标
        输入：
            boardSize：棋盘格内角点数量
            squareSize：棋盘格每个小格的实际尺寸
        输出：
            返回值：位于 z=0 平面上的棋盘格三维角点集合
    */
    std::vector<cv::Point3f> createObjectPoints(const cv::Size& boardSize, const cv::Size2d& squareSize) const;
};

} // namespace htmsr
