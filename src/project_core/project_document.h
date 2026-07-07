// 文件说明：
// 定义项目元数据结构，用于保存项目路径、状态和最近配置。

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "data_model/reconstruction_types.h"

namespace htmsr::project_core {

struct ProjectDocument {
    int version = 1;
    QString name;
    QString rootPath;
    QString manifestPath;
    QString calibrationPath;
    QString imageDirectory;
    QStringList recentOutputFiles;
    data_model::ReconstructionConfig lastConfig;
    QString lastRunStatus;

    [[nodiscard]] QJsonObject toJson() const;
    static ProjectDocument fromJson(const QJsonObject& json);
};

}  // namespace htmsr::project_core
