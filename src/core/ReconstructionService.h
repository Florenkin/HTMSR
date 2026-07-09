#pragma once

#include "core/LaserExtractionService.h"
#include "core/PointCloudService.h"
#include "core/Types.h"

namespace htmsr {

class ReconstructionService {
public:
    /*
        函数功能：批量读取左右重建图像，并对每一对图像执行三维重建
        输入：
            input：离线重建输入参数，包括左右图像目录、标定结果、线提取参数和匹配阈值
        输出：
            返回值：逐帧重建结果和合并后的三维点云
    */
    ReconstructionResult reconstruct(const ReconstructionInput& input) const;

private:
    /*
        函数功能：对单对左右图像执行激光中心线提取、去畸变、匹配和三维点恢复
        输入：
            leftImage：左相机图像
            rightImage：右相机图像
            calibration：双目标定结果
            laserConfig：激光中心线提取参数
            matchDistanceThreshold：左右中心线点匹配误差阈值
        输出：
            leftPreview：左图中心线调试预览图
            rightPreview：右图中心线调试预览图
            返回值：该帧恢复出的三维点集合
    */
    std::vector<Eigen::Vector3d> reconstructFrame(
        const cv::Mat& leftImage,
        const cv::Mat& rightImage,
        const CalibrationResult& calibration,
        const LaserExtractionConfig& laserConfig,
        double matchDistanceThreshold,
        cv::Mat& leftPreview,
        cv::Mat& rightPreview) const;

    /*
        函数功能：将像素坐标根据相机内参转换为归一化相机射线
        输入：
            pixel：像素坐标
            cameraMatrix：相机内参矩阵
        输出：
            返回值：相机坐标系下的射线方向
    */
    Eigen::Vector3d pixelToRay(const Eigen::Vector2d& pixel, const Eigen::Matrix3d& cameraMatrix) const;

    /*
        函数功能：计算两条空间直线最近点的中点，用于三维点恢复
        输入：
            directionA：第一条直线方向
            pointA：第一条直线经过的点
            directionB：第二条直线方向
            pointB：第二条直线经过的点
        输出：
            返回值：两条直线最近点连线的中点
    */
    Eigen::Vector3d closestPointBetweenLines(
        const Eigen::Vector3d& directionA,
        const Eigen::Vector3d& pointA,
        const Eigen::Vector3d& directionB,
        const Eigen::Vector3d& pointB) const;
};

} // namespace htmsr
