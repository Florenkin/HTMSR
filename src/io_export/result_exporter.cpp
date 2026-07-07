// 文件说明：
// 实现重建结果导出逻辑，统一输出 CSV、PLY 和 POI 文件。

#include "io_export/result_exporter.h"

#include <QFile>
#include <QTextStream>

#include <opencv2/imgcodecs.hpp>

#include "io_export/opencorr_poi_codec.h"

namespace htmsr::io_export {

ResultExporter::ResultExporter(const OpenCorrPoiCodec& poiCodec) : poiCodec_(poiCodec) {}

bool ResultExporter::exportCenterlineCsv(const data_model::Point2DList& points, const QString& filePath, QString& error) const {
    // 导出单侧中心线点集，便于和调试预览图互相对照。

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        error = QString("无法写入中心线 CSV: %1").arg(filePath);
        return false;
    }

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);
    out << "x,y\n";
    for (const data_model::Point2D& point : points) {
        out << point.x << ',' << point.y << '\n';
    }
    return true;
}

bool ResultExporter::exportMatchCsv(const data_model::StereoPointMatchList& pairs, const QString& filePath, QString& error) const {
    // 导出左右匹配点和误差，便于分析双目约束效果。

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        error = QString("鏃犳硶鍐欏叆鍖归厤 CSV: %1").arg(filePath);
        return false;
    }

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);
    out << "left_x,left_y,right_x,right_y,error\n";
    for (const data_model::StereoPointMatch& pair : pairs) {
        out << pair.leftPoint.x << ',' << pair.leftPoint.y << ','
            << pair.rightPoint.x << ',' << pair.rightPoint.y << ','
            << pair.error << '\n';
    }
    return true;
}

bool ResultExporter::exportPreviewImage(const cv::Mat& previewImage, const QString& filePath, QString& error) const {
    if (!cv::imwrite(filePath.toStdString(), previewImage)) {
        error = QString("无法写入预览图: %1").arg(filePath);
        return false;
    }
    return true;
}

bool ResultExporter::exportPointCloudPly(const data_model::Point3DList& points, const QString& filePath, QString& error) const {
    // 导出 PLY 点云，用于通用三维软件查看。

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        error = QString("无法写入 PLY: %1").arg(filePath);
        return false;
    }

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);
    out << "ply\nformat ascii 1.0\n";
    out << "element vertex " << points.size() << "\n";
    out << "property float x\nproperty float y\nproperty float z\n";
    out << "end_header\n";

    for (const data_model::Point3D& point : points) {
        out << point.x << ' ' << point.y << ' ' << point.z << '\n';
    }

    return true;
}

bool ResultExporter::exportPointCloudAsPoi3D(const data_model::Point3DList& points, const QString& filePath, QString& error) const {
    QVector<data_model::POI3D> pois;
    pois.reserve(points.size());
    for (const data_model::Point3D& point : points) {
        pois.append(data_model::POI3D(point));
    }
    return poiCodec_.saveTable3D(pois, filePath, error);
}

bool ResultExporter::exportCenterlineAsPoi2D(const data_model::Point2DList& points, const QString& filePath, QString& error) const {
    QVector<data_model::POI2D> pois;
    pois.reserve(points.size());
    for (const data_model::Point2D& point : points) {
        pois.append(data_model::POI2D(point));
    }
    return poiCodec_.saveTable2D(pois, filePath, error);
}

}  // namespace htmsr::io_export
