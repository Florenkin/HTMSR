#pragma once

#include "core/Types.h"

#include <QString>

namespace htmsr::app {

enum class PointCloudExportFormat { Pcd, Txt };

class ResultExportService {
public:
    // 返回实际保存路径；没有扩展名时自动补齐。导出不改变当前结果和自动保存路径。
    static QString exportCalibration(const QString& filename, const CalibrationResult& result);
    static QString exportPointCloud(const QString& filename, const ReconstructionResult& result,
        PointCloudExportFormat defaultFormat = PointCloudExportFormat::Pcd);
};

} // namespace htmsr::app
