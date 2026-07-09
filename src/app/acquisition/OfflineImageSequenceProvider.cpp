#include "app/acquisition/OfflineImageSequenceProvider.h"

#include "core/FileSystemUtils.h"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <stdexcept>

namespace htmsr::app {

OfflineImageSequenceProvider::OfflineImageSequenceProvider(std::string leftDirectory, std::string rightDirectory, ImageRange range)
{
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
    index_ = 0;
}

} // namespace htmsr::app
