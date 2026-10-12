#include "app/acquisition/OfflineImageSequenceProvider.h"

#include "core/FileSystemUtils.h"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <stdexcept>

namespace htmsr::app {

/*
    函数功能：构造离线左右图像序列采集源，并完成左右图像路径配对
    输入：
        leftDirectory：左图像目录
        rightDirectory：右图像目录
        range：图像索引范围
    输出：
        无（构造后保存已配对的左右图像路径）
*/
OfflineImageSequenceProvider::OfflineImageSequenceProvider(std::string leftDirectory, std::string rightDirectory, ImageRange range)
{
    // 离线采集源按目录读取左右图像，并按较短的一侧截断成有效图像对。
    leftPaths_ = listImageFiles(leftDirectory, range);
    rightPaths_ = listImageFiles(rightDirectory, range);
    const size_t paired = std::min(leftPaths_.size(), rightPaths_.size());
    leftPaths_.resize(paired);
    rightPaths_.resize(paired);
}

// 返回离线采集源名称，便于日志和调试显示。
std::string OfflineImageSequenceProvider::name() const
{
    return "Offline Image Sequence";
}

bool OfflineImageSequenceProvider::hasNext() const
{
    return index_ < leftPaths_.size() && index_ < rightPaths_.size();
}

/*
    函数功能：读取当前索引对应的一组左右离线图像
    输入：
        无
    输出：
        返回值：包含左右图像、原始路径和帧序号的同步帧对象
*/
FramePair OfflineImageSequenceProvider::next()
{
    if (!hasNext()) {
        throw std::runtime_error("Offline image sequence has no more frames.");
    }

    // 每次调用返回一组左右图像，同时保留原始路径用于日志和调试。
    FramePair pair;
    pair.leftPath = leftPaths_[index_];
    pair.rightPath = rightPaths_[index_];
    pair.left = cv::imread(pair.leftPath, cv::IMREAD_COLOR);
    pair.right = cv::imread(pair.rightPath, cv::IMREAD_COLOR);
    ++index_;
    return pair;
}

/*
    函数功能：重置离线图像序列读取位置
    输入：
        无
    输出：
        无（函数执行后下次读取会从第一组图像重新开始）
*/
void OfflineImageSequenceProvider::reset()
{
    // 重置读取位置，便于同一组离线图像重复处理。
    index_ = 0;
}

} // namespace htmsr::app
