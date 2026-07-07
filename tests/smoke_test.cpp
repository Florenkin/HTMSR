// 文件说明：
// 最小化冒烟测试，验证项目创建、POI 编解码和双目 manifest 校验。

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

#include "data_model/poi_types.h"
#include "io_export/opencorr_poi_codec.h"
#include "project_core/project_document.h"
#include "project_core/manifest_service.h"
#include "project_core/project_service.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::cerr << "failed to create temp dir" << std::endl;
        return 1;
    }

    htmsr::project_core::ProjectService projectService;
    htmsr::project_core::ProjectDocument document;
    QString error;
    if (!projectService.createProject(tempDir.path(), "smoke", document, error)) {
        std::cerr << error.toStdString() << std::endl;
        return 1;
    }

    htmsr::io_export::OpenCorrPoiCodec codec;
    QVector<htmsr::data_model::POI2D> pois;
    htmsr::data_model::POI2D poi(10.f, 20.f);
    poi.deformation.u = 1.5f;
    poi.deformation.v = 2.5f;
    poi.result.zncc = 0.88f;
    pois.append(poi);

    const QString poiFile = QDir(document.rootPath).filePath("output/poi/test_poi2d.csv");
    if (!codec.saveTable2D(pois, poiFile, error)) {
        std::cerr << error.toStdString() << std::endl;
        return 1;
    }

    const QVector<htmsr::data_model::POI2D> loadedPois = codec.loadTable2D(poiFile, error);
    if (!error.isEmpty() || loadedPois.size() != 1 || std::abs(loadedPois.front().deformation.u - 1.5f) > 0.001f) {
        std::cerr << "poi roundtrip failed" << std::endl;
        return 1;
    }

    auto reopened = projectService.openProject(projectService.projectFilePath(document), error);
    if (!reopened.has_value() || reopened->name != "smoke") {
        std::cerr << "project reopen failed" << std::endl;
        return 1;
    }

    QFile leftImage(QDir(document.imageDirectory).filePath("left/frame_0001.png"));
    QDir().mkpath(QFileInfo(leftImage).absolutePath());
    if (!leftImage.open(QIODevice::WriteOnly)) {
        std::cerr << "failed to create left image placeholder" << std::endl;
        return 1;
    }
    leftImage.close();

    QFile rightImage(QDir(document.imageDirectory).filePath("right/frame_0001.png"));
    QDir().mkpath(QFileInfo(rightImage).absolutePath());
    if (!rightImage.open(QIODevice::WriteOnly)) {
        std::cerr << "failed to create right image placeholder" << std::endl;
        return 1;
    }
    rightImage.close();

    QFile manifest(document.manifestPath);
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        std::cerr << "failed to write manifest" << std::endl;
        return 1;
    }
    manifest.write("left_image_path,right_image_path,scan_index,laser_position,scan_start_position,scan_end_position\n");
    manifest.write("left/frame_0001.png,right/frame_0001.png,0,0.0,0.0,1.0\n");
    manifest.close();

    htmsr::project_core::ManifestService manifestService;
    const auto manifestResult = manifestService.loadAndValidate(document);
    if (!manifestResult.valid || manifestResult.rows.size() != 1) {
        std::cerr << "stereo manifest validation failed" << std::endl;
        return 1;
    }

    std::cout << "smoke test passed" << std::endl;
    return 0;
}
