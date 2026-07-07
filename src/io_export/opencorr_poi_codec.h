// 文件说明：
// 声明 OpenCorr 兼容 POI 表格的读写接口。

#pragma once

#include <QString>
#include <QVector>

#include "data_model/poi_types.h"

namespace htmsr::io_export {

class OpenCorrPoiCodec {
public:
    bool saveTable2D(const QVector<data_model::POI2D>& poiQueue, const QString& filePath, QString& error) const;
    bool saveTable2DS(const QVector<data_model::POI2DS>& poiQueue, const QString& filePath, QString& error) const;
    bool saveTable3D(const QVector<data_model::POI3D>& poiQueue, const QString& filePath, QString& error) const;

    QVector<data_model::POI2D> loadTable2D(const QString& filePath, QString& error) const;
    QVector<data_model::POI2DS> loadTable2DS(const QString& filePath, QString& error) const;
    QVector<data_model::POI3D> loadTable3D(const QString& filePath, QString& error) const;
};

}  // namespace htmsr::io_export
