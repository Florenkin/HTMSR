// 文件说明：
// 实现项目创建、打开、保存以及数据导入逻辑。

#include "project_core/project_service.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace htmsr::project_core {

namespace {

bool ensureDirectory(const QString& path, QString& error) {
    QDir dir;
    if (dir.mkpath(path)) {
        return true;
    }
    error = QString("无法创建目录: %1").arg(path);
    return false;
}

bool copyFileReplacing(const QString& source, const QString& destination, QString& error) {
    if (QFile::exists(destination) && !QFile::remove(destination)) {
        error = QString("无法覆盖文件: %1").arg(destination);
        return false;
    }
    if (!QFile::copy(source, destination)) {
        error = QString("复制文件失败: %1 -> %2").arg(source, destination);
        return false;
    }
    return true;
}

}  // namespace

bool ProjectService::createProject(const QString& parentDirectory, const QString& projectName, ProjectDocument& document, QString& error) const {
    const QString projectRoot = QDir(parentDirectory).filePath(projectName);

    // 初始化项目元数据。

    document = {};
    document.version = 1;
    document.name = projectName;
    document.rootPath = projectRoot;
    document.manifestPath = QDir(projectRoot).filePath("input/manifest.csv");
    document.calibrationPath = QDir(projectRoot).filePath("input/calibration.yml");
    document.imageDirectory = QDir(projectRoot).filePath("input/images");
    document.lastRunStatus = "Idle";

    // 一次性创建输入、输出和日志目录结构。

    const QStringList directories = {
        projectRoot,
        QDir(projectRoot).filePath("input"),
        QDir(projectRoot).filePath("input/images"),
        QDir(projectRoot).filePath("output"),
        QDir(projectRoot).filePath("output/centerline_csv"),
        QDir(projectRoot).filePath("output/centerline_preview"),
        QDir(projectRoot).filePath("output/match_csv"),
        QDir(projectRoot).filePath("output/match_preview"),
        QDir(projectRoot).filePath("output/pointcloud"),
        QDir(projectRoot).filePath("output/poi"),
        QDir(projectRoot).filePath("output/logs")
    };

    for (const QString& path : directories) {
        if (!ensureDirectory(path, error)) {
            return false;
        }
    }

    return saveProject(document, error);
}

std::optional<ProjectDocument> ProjectService::openProject(const QString& projectFilePathValue, QString& error) const {
    QFile file(projectFilePathValue);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("无法打开项目文件: %1").arg(projectFilePathValue);
        return std::nullopt;
    }

    const QJsonDocument jsonDocument = QJsonDocument::fromJson(file.readAll());
    if (!jsonDocument.isObject()) {
        error = QString("项目文件格式无效: %1").arg(projectFilePathValue);
        return std::nullopt;
    }

    ProjectDocument document = ProjectDocument::fromJson(jsonDocument.object());
    if (document.rootPath.isEmpty()) {
        document.rootPath = QFileInfo(projectFilePathValue).absolutePath();
    }
    return document;
}

bool ProjectService::saveProject(const ProjectDocument& document, QString& error) const {
    QFile file(projectFilePath(document));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QString("无法写入项目文件: %1").arg(file.fileName());
        return false;
    }

    const QJsonDocument jsonDocument(document.toJson());
    file.write(jsonDocument.toJson(QJsonDocument::Indented));
    return true;
}

bool ProjectService::importManifest(ProjectDocument& document, const QString& sourceManifestPath, QString& error) const {
    if (!copyFileReplacing(sourceManifestPath, document.manifestPath, error)) {
        return false;
    }
    return saveProject(document, error);
}

bool ProjectService::importCalibration(ProjectDocument& document, const QString& sourceCalibrationPath, QString& error) const {
    if (!copyFileReplacing(sourceCalibrationPath, document.calibrationPath, error)) {
        return false;
    }
    document.lastConfig.calibrationPath = document.calibrationPath;
    return saveProject(document, error);
}

bool ProjectService::importImageDirectory(ProjectDocument& document, const QString& sourceImageDirectory, QString& error) const {
    const QDir sourceDir(sourceImageDirectory);
    if (!sourceDir.exists()) {
        error = QString("图像目录不存在: %1").arg(sourceImageDirectory);
        return false;
    }

    QString imagesDirectoryError;
    if (!ensureDirectory(document.imageDirectory, imagesDirectoryError)) {
        error = imagesDirectoryError;
        return false;
    }

    // 递归复制左右图像目录，保留原有相对层级。

    const QStringList filters = {"*.png", "*.bmp", "*.jpg", "*.jpeg", "*.tif", "*.tiff"};
    QDirIterator iterator(sourceImageDirectory, filters, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo fileInfo = iterator.fileInfo();
        const QString relativePath = sourceDir.relativeFilePath(fileInfo.absoluteFilePath());
        const QString destination = QDir(document.imageDirectory).filePath(relativePath);
        QString destinationError;
        if (!ensureDirectory(QFileInfo(destination).absolutePath(), destinationError)) {
            error = destinationError;
            return false;
        }
        if (!copyFileReplacing(fileInfo.absoluteFilePath(), destination, error)) {
            return false;
        }
    }

    return saveProject(document, error);
}

QString ProjectService::projectFilePath(const ProjectDocument& document) const {
    return QDir(document.rootPath).filePath("project.json");
}

QString ProjectService::inputDirectory(const ProjectDocument& document) const {
    return QDir(document.rootPath).filePath("input");
}

QString ProjectService::outputDirectory(const ProjectDocument& document) const {
    return QDir(document.rootPath).filePath("output");
}

}  // namespace htmsr::project_core
