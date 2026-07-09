#include "app/acquisition/OfflineImageSequenceProvider.h"

#include "core/FileSystemUtils.h"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <stdexcept>

namespace htmsr::app {

OfflineImageSequenceProvider::OfflineImageSequenceProvider(std::string leftDirectory, std::string rightDirectory, ImageRange range)
{
    // 离线采集源按目录读取左右图像，并按较短的一侧截断成有效图像对。
    leftPaths_ = listImageFiles(leftDirectory, range);
    rightPaths_ = listImageFiles(rightDirectory, range);
    const size_t paired = std::min(leftPaths_.size(), rightPaths_.size());
    leftPaths_.resize(paired);
    rightPaths_.resize(paired);
}

std::string OfflineImageSequenceProvider::name() const
{
    return "Offline Image Sequence";
}

bool OfflineImageSequenceProvider::hasNext() const
{
    return index_ < leftPaths_.size() && index_ < rightPaths_.size();
}

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

void OfflineImageSequenceProvider::reset()
{
    // 重置读取位置，便于同一组离线图像重复处理。
    index_ = 0;
}

} // namespace htmsr::app
