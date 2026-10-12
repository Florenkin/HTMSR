#pragma once

#include <QDateTime>

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace htmsr::app {

// 一次重建生成的左右激光线诊断图集合。
struct LaserExtractionImageResult {
    bool enabled = false;
    std::string sessionDirectory;
    std::vector<std::string> leftImagePaths;
    std::vector<std::string> rightImagePaths;
    int failedPairCount = 0;
    std::string warningMessage;
};

// 将重建线程中的左右提线预览按会话成对保存，任何保存失败都只影响诊断图。
class LaserExtractionStorage {
public:
    explicit LaserExtractionStorage(const std::string& rootDirectory,
        const QDateTime& timestamp = QDateTime::currentDateTime());

    bool isReady() const;
    bool savePair(int frameIndex, const cv::Mat& leftPreview, const cv::Mat& rightPreview);
    const LaserExtractionImageResult& result() const;

private:
    void recordFailure(const std::string& message);

    QString leftDirectory_;
    QString rightDirectory_;
    LaserExtractionImageResult result_;
    bool ready_ = false;
};

} // namespace htmsr::app
