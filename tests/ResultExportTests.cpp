#include "app/services/ResultExportService.h"
#include "app/ui/AcquisitionPanel.h"
#include "core/CalibrationService.h"
#include "core/PointCloudService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

using namespace htmsr;
using namespace htmsr::app;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QByteArray readFile(const QString& filename)
{
    QFile file(filename);
    require(file.open(QIODevice::ReadOnly), "Exported file must be readable");
    return file.readAll();
}
}

void runResultExportTests()
{
    QTemporaryDir work;
    require(work.isValid(), "Export tests need an isolated destination");
    CalibrationResult calibration;
    calibration.K1 = (cv::Mat_<double>(3, 3) << 100, 0, 32, 0, 100, 24, 0, 0, 1);
    calibration.K2 = calibration.K1.clone();
    calibration.D1 = calibration.D2 = cv::Mat::zeros(1, 5, CV_64F);
    calibration.R = cv::Mat::eye(3, 3, CV_64F);
    calibration.t = (cv::Mat_<double>(3, 1) << -60, 0, 0);
    calibration.rms = 0.125;

    const QString chosen = QDir(work.path()).filePath(QString::fromUtf8("用户指定路径/标定结果"));
    const QString calibrationFile = ResultExportService::exportCalibration(chosen, calibration);
    require(calibrationFile == chosen + ".yml", "Calibration export must append the default extension");
    cv::FileStorage serialized(readFile(calibrationFile).toStdString(), cv::FileStorage::READ | cv::FileStorage::MEMORY);
    cv::Mat exportedK, exportedT;
    serialized["K1"] >> exportedK;
    serialized["t"] >> exportedT;
    require(cv::norm(exportedK - calibration.K1) < 1e-9 && cv::norm(exportedT - calibration.t) < 1e-9,
        "Calibration export must preserve the current matrices at a Unicode destination");
    const QString yamlFile = QDir(work.path()).filePath("loaded_calibration.yaml");
    ResultExportService::exportCalibration(yamlFile, calibration);
    CalibrationResult loaded;
    require(CalibrationService{}.loadCalibration(yamlFile.toStdString(), loaded) && loaded.isValid(),
        "The exported calibration must load with the production service");
    ResultExportService::exportCalibration(QDir(work.path()).filePath("copied_loaded_calibration.yml"), loaded);

    ReconstructionResult cloud;
    cloud.mergedPoints = {{1.25, 2.5, 3.75}, {-4, 5, 6}};
    cloud.txtPath = "automatic/original.txt";
    cloud.pcdPath = "automatic/original.pcd";
    const QString pcdFile = ResultExportService::exportPointCloud(QDir(work.path()).filePath("chosen_cloud"), cloud);
    const QString txtFile = ResultExportService::exportPointCloud(QDir(work.path()).filePath("chosen_text_cloud"),
        cloud, PointCloudExportFormat::Txt);
    require(pcdFile.endsWith(".pcd") && txtFile.endsWith(".txt"), "Point cloud export must support both selectable default formats");
    for (const auto& filename : {pcdFile, txtFile}) {
        const auto points = PointCloudService{}.load(filename.toStdString());
        require(points.size() == cloud.mergedPoints.size() && (points[0] - cloud.mergedPoints[0]).norm() < 1e-5,
            "Exported PCD and TXT must preserve point count and coordinates");
    }
    const QString unicodePcd = ResultExportService::exportPointCloud(
        QDir(work.path()).filePath(QString::fromUtf8("用户指定路径/点云.pcd")), cloud);
    const QString unicodeTxt = ResultExportService::exportPointCloud(
        QDir(work.path()).filePath(QString::fromUtf8("用户指定路径/点云.txt")), cloud);
    require(readFile(unicodePcd) == readFile(pcdFile) && readFile(unicodeTxt) == readFile(txtFile),
        "Unicode paths and explicit extensions must produce the selected cloud format");
    require(cloud.txtPath == "automatic/original.txt" && cloud.pcdPath == "automatic/original.pcd",
        "Manual export must retain the automatic output paths of the current result");

    const QByteArray historical = readFile(calibrationFile);
    bool rejected = false;
    try {
        ResultExportService::exportCalibration(calibrationFile, {});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && readFile(calibrationFile) == historical, "An invalid result must not overwrite a historical calibration");
    for (const auto& filename : {"empty.pcd", "unsupported.xyz"}) {
        rejected = false;
        try {
            ResultExportService::exportPointCloud(QDir(work.path()).filePath(filename),
                QString(filename).startsWith("empty") ? ReconstructionResult{} : cloud);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && !QFileInfo::exists(QDir(work.path()).filePath(filename)),
            "Empty clouds and unsupported formats must not create output files");
    }
    const QString blockedFile = QDir(work.path()).filePath("blocked");
    QFile blocked(blockedFile);
    require(blocked.open(QIODevice::WriteOnly) && blocked.write("history") == 7, "An unwritable parent fixture must exist");
    blocked.close();
    rejected = false;
    try {
        ResultExportService::exportPointCloud(QDir(blockedFile).filePath("cloud.pcd"), cloud);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && readFile(blockedFile) == "history", "A failed export must leave the obstructing existing file intact");

    AcquisitionPanel panel;
    auto* calibrationExport = panel.findChild<QPushButton*>("exportCalibration");
    auto* reconstructionExport = panel.findChild<QPushButton*>("exportReconstruction");
    require(calibrationExport && reconstructionExport && calibrationExport->text() == QString::fromUtf8("导出") &&
        reconstructionExport->text() == QString::fromUtf8("导出") && !calibrationExport->isEnabled() && !reconstructionExport->isEnabled(),
        "Each page must have an export action disabled before a usable result exists");
    int calibrationRequests = 0, reconstructionRequests = 0;
    QObject::connect(&panel, &AcquisitionPanel::exportCalibrationRequested, [&]() { ++calibrationRequests; });
    QObject::connect(&panel, &AcquisitionPanel::exportReconstructionRequested, [&]() { ++reconstructionRequests; });
    panel.setResultAvailability(true, false);
    require(calibrationExport->isEnabled() && !reconstructionExport->isEnabled(), "Loaded calibration must be independently exportable");
    calibrationExport->click();
    panel.setResultAvailability(true, true);
    reconstructionExport->click();
    require(calibrationRequests == 1 && reconstructionRequests == 1, "Each export action must dispatch the corresponding result");
    panel.setBusy(true);
    require(!calibrationExport->isEnabled() && !reconstructionExport->isEnabled(), "Running operations must prevent exporting incomplete results");
    panel.setBusy(false);
    require(calibrationExport->isEnabled() && reconstructionExport->isEnabled(), "Finishing an operation must restore available exports");
    panel.setResultAvailability(false, false);
    require(!calibrationExport->isEnabled() && !reconstructionExport->isEnabled(), "Invalidating results must disable exports again");
    std::cout << "PASS: calibration/cloud exports, format selection, Unicode paths, reload, failed-write preservation and result availability\n";
}
