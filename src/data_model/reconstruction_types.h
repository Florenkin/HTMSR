// 文件说明：
// 定义双目重建流程中的配置、输入、匹配结果和会话结果类型。

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "data_model/geometry_types.h"
#include "data_model/poi_types.h"

namespace htmsr::data_model {

enum class CenterlineMethod {
    Gray,
    Steger,
    MultiThres
};

struct ReconstructionConfig {
    QString calibrationPath;
    CenterlineMethod centerlineMethod = CenterlineMethod::Gray;
    int thres = 50;
    int slt_thres = 10;
    double bwThr = 55.0;
    double sumThr = 124.0;
    double matchingDistance = 0.1;
    bool useFullLeftRoi = true;
    bool useFullRightRoi = true;
    QVector<int> leftRoi {0, 0, 0, 0};
    QVector<int> rightRoi {0, 0, 0, 0};
};

struct StereoFrameManifestRow {
    QString leftImagePath;
    QString rightImagePath;
    int scanIndex = 0;
    double laserPosition = 0.0;
    double scanStartPosition = 0.0;
    double scanEndPosition = 0.0;
};

struct CenterlineResult {
    QString sourceImagePath;
    QString previewPath;
    QString csvPath;
    Point2DList points;
};

struct StereoPointMatch {
    Point2D leftPoint;
    Point2D rightPoint;
    double error = 0.0;
};

using StereoPointMatchList = std::vector<StereoPointMatch>;

struct StereoMatchResult {
    QString csvPath;
    QString previewPath;
    int pairCount = 0;
};

struct PointCloudResult {
    QString frameName;
    QString plyPath;
    QString poiPath;
    Point3DList points;
};

struct StereoFrameRunResult {
    StereoFrameManifestRow manifestRow;
    bool succeeded = false;
    QString message;
    CenterlineResult leftCenterline;
    CenterlineResult rightCenterline;
    StereoMatchResult stereoMatch;
    PointCloudResult pointCloud;
};

struct ReconstructionSession {
    QString projectName;
    QString projectRoot;
    QString manifestPath;
    QString calibrationPath;
    QString imageDirectory;
    ReconstructionConfig config;
    QVector<StereoFrameManifestRow> manifestRows;
    QVector<StereoFrameRunResult> frameResults;
    QVector<int> failedFrameIndices;
    QString mergedPointCloudPath;
    QString mergedPoiPath;
};

struct ManifestValidationResult {
    bool valid = false;
    QStringList messages;
    QVector<StereoFrameManifestRow> rows;
};

}  // namespace htmsr::data_model
