#pragma once

#include "core/Types.h"

namespace htmsr {

class LaserExtractionService {
public:
    /*
        函数功能：根据配置在指定 ROI 内提取激光中心线
        输入：
            image：输入图像
            roi：待处理的图像区域
            config：激光中心线提取参数
        输出：
            返回值：中心线点集以及叠加显示的调试预览图
    */
    LaserExtractionResult extract(const cv::Mat& image, const cv::Rect& roi, const LaserExtractionConfig& config) const;

private:
    /*
        函数功能：使用灰度重心法提取激光条纹中心线
        输入：
            image：输入图像
            roi：待处理的图像区域
            config：灰度阈值、颜色通道、端点剔除等参数
        输出：
            返回值：中心线点集以及调试预览图
    */
    LaserExtractionResult extractGrayCentroid(const cv::Mat& image, const cv::Rect& roi, const LaserExtractionConfig& config) const;

    /*
        函数功能：使用 Steger 法提取激光条纹亚像素中心线
        输入：
            image：输入图像
            roi：待处理的图像区域
            config：二值阈值、筛选阈值、线宽和端点剔除等参数
        输出：
            返回值：中心线点集以及调试预览图
    */
    LaserExtractionResult extractSteger(const cv::Mat& image, const cv::Rect& roi, const LaserExtractionConfig& config) const;

    /*
        函数功能：将用户输入的 ROI 裁剪到图像有效范围内
        输入：
            image：输入图像
            roi：原始 ROI
        输出：
            返回值：位于图像边界内的安全 ROI
    */
    cv::Rect clampRoi(const cv::Mat& image, const cv::Rect& roi) const;
};

} // namespace htmsr
