// 文件说明：
// 声明重建结果导出器，负责导出中心线、匹配和点云文件。

#pragma once

#include <QString>
#include <QVector>

#include <opencv2/core.hpp>

#include "data_model/reconstruction_types.h"

namespace htmsr::io_export {

class OpenCorrPoiCodec;

class ResultExporter {
public:
    explicit ResultExporter(const OpenCorrPoiCodec& poiCodec);

    bool exportCenterlineCsv(const data_model::Point2DList& points, const QString& filePath, QString& error) const;
    bool exportMatchCsv(const data_model::StereoPointMatchList& pairs, const QString& filePath, QString& error) const;
    bool exportPreviewImage(const cv::Mat& previewImage, const QString& filePath, QString& error) const;
    bool exportPointCloudPly(const data_model::Point3DList& points, const QString& filePath, QString& error) const;
    bool exportPointCloudAsPoi3D(const data_model::Point3DList& points, const QString& filePath, QString& error) const;
    bool exportCenterlineAsPoi2D(const data_model::Point2DList& points, const QString& filePath, QString& error) const;

private:
    const OpenCorrPoiCodec& poiCodec_;
};

}  // namespace htmsr::io_export
