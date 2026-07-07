// 文件说明：
// 实现双目 manifest 的解析、路径补全和合法性校验。

#include "project_core/manifest_service.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace htmsr::project_core {

namespace {

QString resolveImagePath(const ProjectDocument& document, const QString& rawPath) {
    // 先尊重绝对路径。

    const QFileInfo fileInfo(rawPath);
    if (fileInfo.isAbsolute()) {
        return rawPath;
    }

    // 再尝试解析为项目 images 目录下的相对路径。

    const QString fromProjectImages = QDir(document.imageDirectory).filePath(rawPath);
    if (QFileInfo::exists(fromProjectImages)) {
        return fromProjectImages;
    }

    // 最后退回到 manifest 所在目录做相对路径解析。

    return QDir(QFileInfo(document.manifestPath).absolutePath()).filePath(rawPath);
}

int headerIndex(const QStringList& headers, const QString& name) {
    return headers.indexOf(name);
}

}  // namespace

data_model::ManifestValidationResult ManifestService::loadAndValidate(const ProjectDocument& document) const {
    data_model::ManifestValidationResult result;

    // 读取 manifest 原始文本内容。

    QFile file(document.manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.messages.append(QString("无法打开 manifest: %1").arg(document.manifestPath));
        return result;
    }

    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        result.messages.append("manifest 文件为空");
        return result;
    }

    // 解析表头并定位双目重建必需字段。

    const QStringList headers = lines.front().split(',', Qt::KeepEmptyParts);
    const int leftImageIndex = headerIndex(headers, "left_image_path");
    const int rightImageIndex = headerIndex(headers, "right_image_path");
    const int scanIndex = headerIndex(headers, "scan_index");
    const int laserIndex = headerIndex(headers, "laser_position");
    const int startIndex = headerIndex(headers, "scan_start_position");
    const int endIndex = headerIndex(headers, "scan_end_position");

    if (leftImageIndex < 0 || rightImageIndex < 0 || scanIndex < 0 || laserIndex < 0 || startIndex < 0 || endIndex < 0) {
        result.messages.append(
            "manifest 缺少必需表头: left_image_path, right_image_path, scan_index, laser_position, scan_start_position, scan_end_position");
        return result;
    }

    // 逐行校验左右图像路径和扫描参数。

    bool hasError = false;
    for (int row = 1; row < lines.size(); ++row) {
        const QStringList cells = lines[row].split(',', Qt::KeepEmptyParts);
        if (cells.size() < headers.size()) {
            result.messages.append(QString("第 %1 行列数不足").arg(row + 1));
            hasError = true;
            continue;
        }

        data_model::StereoFrameManifestRow manifestRow;
        manifestRow.leftImagePath = resolveImagePath(document, cells[leftImageIndex].trimmed());
        manifestRow.rightImagePath = resolveImagePath(document, cells[rightImageIndex].trimmed());

        bool okScan = false;
        bool okLaser = false;
        bool okStart = false;
        bool okEnd = false;
        manifestRow.scanIndex = cells[scanIndex].trimmed().toInt(&okScan);
        manifestRow.laserPosition = cells[laserIndex].trimmed().toDouble(&okLaser);
        manifestRow.scanStartPosition = cells[startIndex].trimmed().toDouble(&okStart);
        manifestRow.scanEndPosition = cells[endIndex].trimmed().toDouble(&okEnd);

        if (!okScan || !okLaser || !okStart || !okEnd) {
            result.messages.append(QString("第 %1 行包含无效数值").arg(row + 1));
            hasError = true;
            continue;
        }

        if (!QFileInfo::exists(manifestRow.leftImagePath)) {
            result.messages.append(QString("第 %1 行左图像不存在: %2").arg(row + 1).arg(manifestRow.leftImagePath));
            hasError = true;
            continue;
        }

        if (!QFileInfo::exists(manifestRow.rightImagePath)) {
            result.messages.append(QString("第 %1 行右图像不存在: %2").arg(row + 1).arg(manifestRow.rightImagePath));
            hasError = true;
            continue;
        }

        result.rows.append(manifestRow);
    }

    if (result.rows.isEmpty()) {
        result.messages.append("manifest 中没有可处理的有效双目图像对");
    }

    result.valid = !hasError && !result.rows.isEmpty();
    if (result.valid) {
        result.messages.append(QString("manifest 校验通过，共 %1 对双目图像").arg(result.rows.size()));
    }

    return result;
}

QString ManifestService::manifestTemplate() const {
    return "left_image_path,right_image_path,scan_index,laser_position,scan_start_position,scan_end_position\n"
           "left/frame_0001.png,right/frame_0001.png,0,0.0,0.0,10.0\n"
           "left/frame_0002.png,right/frame_0002.png,1,1.0,0.0,10.0\n";
}

}  // namespace htmsr::project_core
