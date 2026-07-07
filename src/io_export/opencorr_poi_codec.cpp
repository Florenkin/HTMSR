// 文件说明：
// 实现 OpenCorr 兼容的二维、三维 POI CSV 编解码。

#include "io_export/opencorr_poi_codec.h"

#include <QFile>
#include <QTextStream>

namespace htmsr::io_export {

namespace {

QStringList splitCsvLine(const QString& line) {
    return line.split(',', Qt::KeepEmptyParts);
}

template <typename PoiType>
bool openWriter(const QString& filePath, QFile& file, QTextStream*& stream, QString& error) {
    file.setFileName(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        error = QString("无法写入文件: %1").arg(filePath);
        stream = nullptr;
        return false;
    }
    stream = new QTextStream(&file);
    stream->setRealNumberNotation(QTextStream::FixedNotation);
    stream->setRealNumberPrecision(8);
    return true;
}

bool openReader(const QString& filePath, QFile& file, QTextStream*& stream, QString& error) {
    file.setFileName(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QString("无法读取文件: %1").arg(filePath);
        stream = nullptr;
        return false;
    }
    stream = new QTextStream(&file);
    return true;
}

}  // namespace

bool OpenCorrPoiCodec::saveTable2D(const QVector<data_model::POI2D>& poiQueue, const QString& filePath, QString& error) const {
    QFile file;
    QTextStream* out = nullptr;
    if (!openWriter<data_model::POI2D>(filePath, file, out, error)) {
        return false;
    }

    *out << "x,y,u,v,u0,v0,ZNCC,iteration,convergence,feature,exx,eyy,exy,subset_rx,subset_ry\n";
    for (const data_model::POI2D& poi : poiQueue) {
        *out << poi.x << ',' << poi.y << ','
             << poi.deformation.u << ',' << poi.deformation.v << ',';
        for (int i = 0; i < 6; ++i) {
            *out << poi.result.r[i] << ',';
        }
        for (int i = 0; i < 3; ++i) {
            *out << poi.strain.e[i] << ',';
        }
        *out << poi.subset_radius.x << ',' << poi.subset_radius.y << '\n';
    }

    delete out;
    return true;
}

bool OpenCorrPoiCodec::saveTable2DS(const QVector<data_model::POI2DS>& poiQueue, const QString& filePath, QString& error) const {
    QFile file;
    QTextStream* out = nullptr;
    if (!openWriter<data_model::POI2DS>(filePath, file, out, error)) {
        return false;
    }

    *out << "x,y,u,v,w,r1r2 ZNCC,r1t1 ZNCC,r1t2 ZNCC,r2_x,r2_y,t1_x,t1_y,t2_x,t2_y,"
            "ref_x,ref_y,ref_z,tar_x,tar_y,tar_z,exx,eyy,ezz,exy,eyz,ezx,subset_rx,subset_ry\n";
    for (const data_model::POI2DS& poi : poiQueue) {
        *out << poi.x << ',' << poi.y << ',';
        for (int i = 0; i < 3; ++i) {
            *out << poi.deformation.p[i] << ',';
        }
        for (int i = 0; i < 9; ++i) {
            *out << poi.result.r[i] << ',';
        }
        *out << poi.ref_coor.x << ',' << poi.ref_coor.y << ',' << poi.ref_coor.z << ','
             << poi.tar_coor.x << ',' << poi.tar_coor.y << ',' << poi.tar_coor.z << ',';
        for (int i = 0; i < 6; ++i) {
            *out << poi.strain.e[i] << ',';
        }
        *out << poi.subset_radius.x << ',' << poi.subset_radius.y << '\n';
    }

    delete out;
    return true;
}

bool OpenCorrPoiCodec::saveTable3D(const QVector<data_model::POI3D>& poiQueue, const QString& filePath, QString& error) const {
    QFile file;
    QTextStream* out = nullptr;
    if (!openWriter<data_model::POI3D>(filePath, file, out, error)) {
        return false;
    }

    *out << "x,y,z,u,ux,uy,uz,v,vx,vy,vz,w,wx,wy,wz,u0,v0,w0,ZNCC,iteration,convergence,feature,"
            "exx,eyy,ezz,exy,eyz,ezx,subset_rx,subset_ry,subset_rz\n";
    for (const data_model::POI3D& poi : poiQueue) {
        *out << poi.x << ',' << poi.y << ',' << poi.z << ',';
        for (int i = 0; i < 12; ++i) {
            *out << poi.deformation.p[i] << ',';
        }
        for (int i = 0; i < 7; ++i) {
            *out << poi.result.r[i] << ',';
        }
        for (int i = 0; i < 6; ++i) {
            *out << poi.strain.e[i] << ',';
        }
        *out << poi.subset_radius.x << ',' << poi.subset_radius.y << ',' << poi.subset_radius.z << '\n';
    }

    delete out;
    return true;
}

QVector<data_model::POI2D> OpenCorrPoiCodec::loadTable2D(const QString& filePath, QString& error) const {
    QFile file;
    QTextStream* in = nullptr;
    QVector<data_model::POI2D> pois;
    if (!openReader(filePath, file, in, error)) {
        return pois;
    }

    if (in->atEnd()) {
        delete in;
        return pois;
    }
    in->readLine();

    while (!in->atEnd()) {
        const QStringList cells = splitCsvLine(in->readLine());
        if (cells.size() < 15) {
            continue;
        }
        data_model::POI2D poi(cells[0].toFloat(), cells[1].toFloat());
        poi.deformation.u = cells[2].toFloat();
        poi.deformation.v = cells[3].toFloat();
        for (int i = 0; i < 6; ++i) {
            poi.result.r[i] = cells[4 + i].toFloat();
        }
        for (int i = 0; i < 3; ++i) {
            poi.strain.e[i] = cells[10 + i].toFloat();
        }
        poi.subset_radius.x = cells[13].toFloat();
        poi.subset_radius.y = cells[14].toFloat();
        pois.append(poi);
    }

    delete in;
    return pois;
}

QVector<data_model::POI2DS> OpenCorrPoiCodec::loadTable2DS(const QString& filePath, QString& error) const {
    QFile file;
    QTextStream* in = nullptr;
    QVector<data_model::POI2DS> pois;
    if (!openReader(filePath, file, in, error)) {
        return pois;
    }

    if (in->atEnd()) {
        delete in;
        return pois;
    }
    in->readLine();

    while (!in->atEnd()) {
        const QStringList cells = splitCsvLine(in->readLine());
        if (cells.size() < 28) {
            continue;
        }

        data_model::POI2DS poi(cells[0].toFloat(), cells[1].toFloat());
        int index = 2;
        for (int i = 0; i < 3; ++i) {
            poi.deformation.p[i] = cells[index++].toFloat();
        }
        for (int i = 0; i < 9; ++i) {
            poi.result.r[i] = cells[index++].toFloat();
        }
        poi.ref_coor = {cells[index++].toFloat(), cells[index++].toFloat(), cells[index++].toFloat()};
        poi.tar_coor = {cells[index++].toFloat(), cells[index++].toFloat(), cells[index++].toFloat()};
        for (int i = 0; i < 6; ++i) {
            poi.strain.e[i] = cells[index++].toFloat();
        }
        poi.subset_radius.x = cells[index++].toFloat();
        poi.subset_radius.y = cells[index++].toFloat();
        pois.append(poi);
    }

    delete in;
    return pois;
}

QVector<data_model::POI3D> OpenCorrPoiCodec::loadTable3D(const QString& filePath, QString& error) const {
    QFile file;
    QTextStream* in = nullptr;
    QVector<data_model::POI3D> pois;
    if (!openReader(filePath, file, in, error)) {
        return pois;
    }

    if (in->atEnd()) {
        delete in;
        return pois;
    }
    in->readLine();

    while (!in->atEnd()) {
        const QStringList cells = splitCsvLine(in->readLine());
        if (cells.size() < 31) {
            continue;
        }

        data_model::POI3D poi(cells[0].toFloat(), cells[1].toFloat(), cells[2].toFloat());
        int index = 3;
        for (int i = 0; i < 12; ++i) {
            poi.deformation.p[i] = cells[index++].toFloat();
        }
        for (int i = 0; i < 7; ++i) {
            poi.result.r[i] = cells[index++].toFloat();
        }
        for (int i = 0; i < 6; ++i) {
            poi.strain.e[i] = cells[index++].toFloat();
        }
        poi.subset_radius.x = cells[index++].toFloat();
        poi.subset_radius.y = cells[index++].toFloat();
        poi.subset_radius.z = cells[index++].toFloat();
        pois.append(poi);
    }

    delete in;
    return pois;
}

}  // namespace htmsr::io_export
