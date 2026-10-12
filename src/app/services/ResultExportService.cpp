#include "app/services/ResultExportService.h"

#include "core/CalibrationService.h"
#include "core/Logger.h"
#include "core/PointCloudService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>

#include <stdexcept>

namespace htmsr::app {
namespace {

QString outputFilename(const QString& filename, const QString& defaultSuffix)
{
    if (filename.isEmpty()) {
        throw std::runtime_error("请选择导出文件路径。");
    }
    return QFileInfo(filename).suffix().isEmpty() ? filename + "." + defaultSuffix : filename;
}

template <typename Writer>
void writeExport(const QString& filename, Writer&& writer)
{
    // 核心服务先写临时文件，再通过 Qt 提交目标文件，支持中文路径并避免失败时损坏已有文件。
    QTemporaryDir temporary(QDir::temp().filePath("htmsr_export_XXXXXX"));
    if (!temporary.isValid()) {
        throw std::runtime_error("无法创建导出临时目录：" + temporary.errorString().toStdString());
    }
    const QString temporaryFile = temporary.filePath("result." + QFileInfo(filename).suffix().toLower());
    writer(QFile::encodeName(temporaryFile).toStdString());

    QFile source(temporaryFile);
    if (!source.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("无法读取导出结果：" + source.errorString().toStdString());
    }
    if (!QDir().mkpath(QFileInfo(filename).absolutePath())) {
        throw std::runtime_error("无法创建导出目录：" + QFileInfo(filename).absolutePath().toStdString());
    }
    QSaveFile destination(filename);
    if (!destination.open(QIODevice::WriteOnly)) {
        throw std::runtime_error("无法保存导出文件：" + filename.toStdString() + "：" + destination.errorString().toStdString());
    }
    while (!source.atEnd()) {
        const QByteArray bytes = source.read(64 * 1024);
        if (bytes.isEmpty() || destination.write(bytes) != bytes.size()) {
            throw std::runtime_error("写入导出文件失败：" + filename.toStdString());
        }
    }
    if (!destination.commit()) {
        throw std::runtime_error("提交导出文件失败：" + filename.toStdString() + "：" + destination.errorString().toStdString());
    }
}

} // namespace

QString ResultExportService::exportCalibration(const QString& filename, const CalibrationResult& result)
{
    if (!result.isValid()) {
        throw std::runtime_error("当前没有可导出的标定结果，请先完成标定或加载有效标定文件。");
    }
    const QString output = outputFilename(filename, "yml");
    const QString suffix = QFileInfo(output).suffix().toLower();
    if (suffix != "yml" && suffix != "yaml") {
        throw std::runtime_error("标定结果请选择 YML 或 YAML 格式。");
    }
    writeExport(output, [&](const std::string& temporary) { CalibrationService{}.saveCalibration(temporary, result, false); });
    Logger::instance().info("Calibration", "标定结果已导出：" + output.toStdString());
    return output;
}

QString ResultExportService::exportPointCloud(const QString& filename, const ReconstructionResult& result,
    PointCloudExportFormat defaultFormat)
{
    if (!result.success || result.mergedPoints.empty()) {
        throw std::runtime_error("当前没有可导出的点云，请先完成重建。");
    }
    const QString output = outputFilename(filename, defaultFormat == PointCloudExportFormat::Txt ? "txt" : "pcd");
    const QString suffix = QFileInfo(output).suffix().toLower();
    if (suffix != "pcd" && suffix != "txt") {
        throw std::runtime_error("点云请选择 PCD 或 TXT 格式。");
    }
    writeExport(output, [&](const std::string& temporary) {
        PointCloudService service;
        if (suffix == "txt") {
            service.saveTxt(temporary, result.mergedPoints, false);
        } else {
            service.savePcd(temporary, result.mergedPoints, false);
        }
    });
    Logger::instance().info("PointCloud", "点云已导出：" + output.toStdString());
    return output;
}

} // namespace htmsr::app
