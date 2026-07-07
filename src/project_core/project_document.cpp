// 文件说明：
// 实现项目元数据与 JSON 之间的序列化和反序列化。

#include "project_core/project_document.h"

#include <QJsonArray>

namespace htmsr::project_core {

namespace {

QString centerlineMethodToString(const data_model::CenterlineMethod method) {
    switch (method) {
    case data_model::CenterlineMethod::Gray:
        return "Gray";
    case data_model::CenterlineMethod::Steger:
        return "Steger";
    case data_model::CenterlineMethod::MultiThres:
        return "MultiThres";
    }
    return "Gray";
}

data_model::CenterlineMethod centerlineMethodFromString(const QString& value) {
    if (value.compare("Steger", Qt::CaseInsensitive) == 0) {
        return data_model::CenterlineMethod::Steger;
    }
    if (value.compare("MultiThres", Qt::CaseInsensitive) == 0) {
        return data_model::CenterlineMethod::MultiThres;
    }
    return data_model::CenterlineMethod::Gray;
}

}  // namespace

QJsonObject ProjectDocument::toJson() const {
    QJsonObject json;
    json["version"] = version;
    json["name"] = name;
    json["rootPath"] = rootPath;
    json["manifestPath"] = manifestPath;
    json["calibrationPath"] = calibrationPath;
    json["imageDirectory"] = imageDirectory;
    json["lastRunStatus"] = lastRunStatus;

    QJsonArray outputs;
    for (const QString& path : recentOutputFiles) {
        outputs.append(path);
    }
    json["recentOutputFiles"] = outputs;

    QJsonObject config;
    config["calibrationPath"] = lastConfig.calibrationPath;
    config["centerlineMethod"] = centerlineMethodToString(lastConfig.centerlineMethod);
    config["thres"] = lastConfig.thres;
    config["slt_thres"] = lastConfig.slt_thres;
    config["bwThr"] = lastConfig.bwThr;
    config["sumThr"] = lastConfig.sumThr;
    config["matchingDistance"] = lastConfig.matchingDistance;
    config["useFullLeftRoi"] = lastConfig.useFullLeftRoi;
    config["useFullRightRoi"] = lastConfig.useFullRightRoi;

    QJsonArray leftRoi;
    for (const int value : lastConfig.leftRoi) {
        leftRoi.append(value);
    }
    config["leftRoi"] = leftRoi;

    QJsonArray rightRoi;
    for (const int value : lastConfig.rightRoi) {
        rightRoi.append(value);
    }
    config["rightRoi"] = rightRoi;
    json["lastConfig"] = config;

    return json;
}

ProjectDocument ProjectDocument::fromJson(const QJsonObject& json) {
    ProjectDocument document;
    document.version = json["version"].toInt(1);
    document.name = json["name"].toString();
    document.rootPath = json["rootPath"].toString();
    document.manifestPath = json["manifestPath"].toString();
    document.calibrationPath = json["calibrationPath"].toString();
    document.imageDirectory = json["imageDirectory"].toString();
    document.lastRunStatus = json["lastRunStatus"].toString();

    const QJsonArray outputs = json["recentOutputFiles"].toArray();
    for (const QJsonValue& value : outputs) {
        document.recentOutputFiles.append(value.toString());
    }

    const QJsonObject config = json["lastConfig"].toObject();
    document.lastConfig.calibrationPath = config["calibrationPath"].toString();
    document.lastConfig.centerlineMethod = centerlineMethodFromString(config["centerlineMethod"].toString());
    document.lastConfig.thres = config["thres"].toInt(50);
    document.lastConfig.slt_thres = config["slt_thres"].toInt(10);
    document.lastConfig.bwThr = config["bwThr"].toDouble(55.0);
    document.lastConfig.sumThr = config["sumThr"].toDouble(124.0);
    document.lastConfig.matchingDistance = config["matchingDistance"].toDouble(0.1);
    document.lastConfig.useFullLeftRoi = config["useFullLeftRoi"].toBool(true);
    document.lastConfig.useFullRightRoi = config["useFullRightRoi"].toBool(true);

    document.lastConfig.leftRoi.clear();
    const QJsonArray leftRoi = config["leftRoi"].toArray();
    for (const QJsonValue& value : leftRoi) {
        document.lastConfig.leftRoi.append(value.toInt());
    }
    if (document.lastConfig.leftRoi.isEmpty()) {
        document.lastConfig.leftRoi = {0, 0, 0, 0};
    }

    document.lastConfig.rightRoi.clear();
    const QJsonArray rightRoi = config["rightRoi"].toArray();
    for (const QJsonValue& value : rightRoi) {
        document.lastConfig.rightRoi.append(value.toInt());
    }
    if (document.lastConfig.rightRoi.isEmpty()) {
        document.lastConfig.rightRoi = {0, 0, 0, 0};
    }

    return document;
}

}  // namespace htmsr::project_core
