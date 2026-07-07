// 文件说明：
// 声明项目目录、输入文件和输出目录的管理服务。

#pragma once

#include <optional>

#include "project_core/project_document.h"

namespace htmsr::project_core {

class ProjectService {
public:
    bool createProject(const QString& parentDirectory, const QString& projectName, ProjectDocument& document, QString& error) const;
    std::optional<ProjectDocument> openProject(const QString& projectFilePath, QString& error) const;
    bool saveProject(const ProjectDocument& document, QString& error) const;
    bool importManifest(ProjectDocument& document, const QString& sourceManifestPath, QString& error) const;
    bool importCalibration(ProjectDocument& document, const QString& sourceCalibrationPath, QString& error) const;
    bool importImageDirectory(ProjectDocument& document, const QString& sourceImageDirectory, QString& error) const;

    [[nodiscard]] QString projectFilePath(const ProjectDocument& document) const;
    [[nodiscard]] QString inputDirectory(const ProjectDocument& document) const;
    [[nodiscard]] QString outputDirectory(const ProjectDocument& document) const;
};

}  // namespace htmsr::project_core
