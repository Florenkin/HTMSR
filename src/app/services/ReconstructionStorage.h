#pragma once

#include "core/Types.h"

#include <QDateTime>
#include <QString>

#include <string>

namespace htmsr::app {

// 重建采集与点云共用的保存规则；时间戳精确到毫秒，重名时追加序号。
class ReconstructionStorage {
public:
    static QString createCaptureDirectory(const std::string& outputDirectory,
        const QDateTime& timestamp = QDateTime::currentDateTime());
    static QString resultsDirectory(const std::string& outputDirectory);
    static void savePointClouds(const std::string& outputDirectory, ReconstructionResult& result,
        const std::string& captureSessionDirectory = {},
        const QDateTime& timestamp = QDateTime::currentDateTime());
};

} // namespace htmsr::app
