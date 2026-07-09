#pragma once

#include "core/Types.h"

namespace htmsr {

class LaserExtractionService {
public:
    LaserExtractionResult extract(const cv::Mat& image, const cv::Rect& roi, const LaserExtractionConfig& config) const;

private:
    LaserExtractionResult extractGrayCentroid(const cv::Mat& image, const cv::Rect& roi, const LaserExtractionConfig& config) const;
    LaserExtractionResult extractSteger(const cv::Mat& image, const cv::Rect& roi, const LaserExtractionConfig& config) const;

    cv::Rect clampRoi(const cv::Mat& image, const cv::Rect& roi) const;
};

} // namespace htmsr
