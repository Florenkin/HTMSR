#include "app/services/ReconstructionStorage.h"

#include "core/PointCloudService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <stdexcept>

namespace htmsr::app {
namespace {

QString storageDirectory(const std::string& outputDirectory, const QString& subdirectory)
{
    const QString root = QString::fromStdString(outputDirectory).trimmed();
    const QString directory = QDir(root.isEmpty() ? "." : root).filePath(subdirectory);
    if (!QDir().mkpath(directory)) {
        throw std::runtime_error("Failed to create reconstruction directory: " + directory.toStdString());
    }
    return directory;
}

QString timestampName(const QDateTime& timestamp)
{
    if (!timestamp.isValid()) {
        throw std::runtime_error("Invalid reconstruction timestamp.");
    }
    return timestamp.toString("yyyyMMdd_HHmmss_zzz");
}

QString uniqueName(const QString& base, int suffix)
{
    return suffix == 0 ? base : base + "_" + QString::number(suffix);
}

QString captureTimestamp(const std::string& captureSessionDirectory, const QDateTime& fallback)
{
    // 兼容已经保存过的 reconstruction_capture_<时间戳>_<标识> 目录。
    const QString name = QFileInfo(QString::fromStdString(captureSessionDirectory)).fileName();
    const auto match = QRegularExpression(
        "^(?:reconstruction_capture_)?(\\d{8}_\\d{6}_\\d{3}(?:_[A-Za-z0-9]+)?)$").match(name);
    return match.hasMatch() ? match.captured(1) : timestampName(fallback);
}

} // namespace

QString ReconstructionStorage::createCaptureDirectory(const std::string& outputDirectory, const QDateTime& timestamp)
{
    const QDir parent(storageDirectory(outputDirectory, "reconstruction/capture"));
    const QString base = timestampName(timestamp);
    for (int suffix = 0; ; ++suffix) {
        const QString name = uniqueName(base, suffix);
        if (parent.mkdir(name)) {
            return parent.filePath(name);
        }
        if (!QFileInfo::exists(parent.filePath(name))) {
            throw std::runtime_error("Failed to create capture session: " + parent.filePath(name).toStdString());
        }
    }
}

QString ReconstructionStorage::resultsDirectory(const std::string& outputDirectory)
{
    return storageDirectory(outputDirectory, "reconstruction/result");
}

void ReconstructionStorage::savePointClouds(const std::string& outputDirectory, ReconstructionResult& result,
    const std::string& captureSessionDirectory, const QDateTime& timestamp)
{
    if (!result.success || result.mergedPoints.empty()) {
        return;
    }

    const QDir parent(resultsDirectory(outputDirectory));
    const QString base = "point_cloud_" + captureTimestamp(captureSessionDirectory, timestamp);
    for (int suffix = 0; ; ++suffix) {
        const QString name = uniqueName(base, suffix);
        const QString txtPath = parent.filePath(name + ".txt");
        const QString pcdPath = parent.filePath(name + ".pcd");
        if (QFileInfo::exists(txtPath) || QFileInfo::exists(pcdPath)) {
            continue;
        }

        // 独占创建两种格式的文件，避免重名或并行保存覆盖历史结果。
        QFile txtFile(txtPath);
        if (!txtFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (QFileInfo::exists(txtPath)) {
                continue;
            }
            throw std::runtime_error("Failed to create point cloud TXT: " + txtPath.toStdString() + ": " + txtFile.errorString().toStdString());
        }
        txtFile.close();
        QFile pcdFile(pcdPath);
        if (!pcdFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            QFile::remove(txtPath);
            if (QFileInfo::exists(pcdPath)) {
                continue;
            }
            throw std::runtime_error("Failed to create point cloud PCD: " + pcdPath.toStdString() + ": " + pcdFile.errorString().toStdString());
        }
        pcdFile.close();

        try {
            PointCloudService service;
            service.saveTxt(txtPath.toStdString(), result.mergedPoints);
            service.savePcd(pcdPath.toStdString(), result.mergedPoints);
        } catch (...) {
            // 仅清理这次独占创建的失败输出，已有点云不受影响。
            QFile::remove(txtPath);
            QFile::remove(pcdPath);
            throw;
        }
        result.txtPath = txtPath.toStdString();
        result.pcdPath = pcdPath.toStdString();
        return;
    }
}

} // namespace htmsr::app
