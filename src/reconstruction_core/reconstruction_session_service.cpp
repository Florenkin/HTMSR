// 文件说明：
// 实现双目离线重建主流程的会话编排。

#include "reconstruction_core/reconstruction_session_service.h"

#include <QDir>
#include <QFileInfo>

#include <opencv2/imgcodecs.hpp>

#include "algorithms/centerline_extractor_factory.h"

namespace htmsr::reconstruction_core {

namespace {

cv::Rect roiFromValues(const QVector<int>& roiValues, const cv::Size& imageSize, bool useFullRoi) {
    if (useFullRoi || roiValues.size() < 4) {
        return cv::Rect(0, 0, imageSize.width, imageSize.height);
    }
    const cv::Rect requested(roiValues[0], roiValues[1], roiValues[2], roiValues[3]);
    return requested & cv::Rect(0, 0, imageSize.width, imageSize.height);
}

QString frameBaseName(const data_model::StereoFrameManifestRow& row) {
    const QString leftName = QFileInfo(row.leftImagePath).completeBaseName();
    const QString rightName = QFileInfo(row.rightImagePath).completeBaseName();
    return leftName == rightName ? leftName : (leftName + "__" + rightName);
}

}  // namespace

ReconstructionSessionService::ReconstructionSessionService(
    const project_core::ManifestService& manifestService,
    const ICalibrationLoader& calibrationLoader,
    const IMatcher& matcher,
    const IReconstructionEngine& reconstructionEngine,
    const io_export::ResultExporter& resultExporter)
    : manifestService_(manifestService),
      calibrationLoader_(calibrationLoader),
      matcher_(matcher),
      reconstructionEngine_(reconstructionEngine),
      resultExporter_(resultExporter) {}

bool ReconstructionSessionService::run(
    const project_core::ProjectDocument& document,
    data_model::ReconstructionSession& session,
    QStringList& logs) const {
    // 初始化本次会话的元数据。

    session = {};
    session.projectName = document.name;
    session.projectRoot = document.rootPath;
    session.manifestPath = document.manifestPath;
    session.calibrationPath = document.calibrationPath;
    session.imageDirectory = document.imageDirectory;
    session.config = document.lastConfig;

    // 第一步：校验 manifest，确认左右图像和扫描参数可用。

    const data_model::ManifestValidationResult manifestResult = manifestService_.loadAndValidate(document);
    logs.append(manifestResult.messages);
    if (!manifestResult.valid) {
        return false;
    }
    session.manifestRows = manifestResult.rows;

    // 第二步：加载双目标定参数。

    StereoCalibrationData calibration;
    QString calibrationError;
    if (!calibrationLoader_.load(document.calibrationPath, calibration, calibrationError)) {
        logs.append(calibrationError);
        return false;
    }
    logs.append(QString("双目标定加载成功: %1").arg(document.calibrationPath));

    const QString centerlineCsvDir = QDir(document.rootPath).filePath("output/centerline_csv");
    const QString centerlinePreviewDir = QDir(document.rootPath).filePath("output/centerline_preview");
    const QString matchCsvDir = QDir(document.rootPath).filePath("output/match_csv");
    const QString matchPreviewDir = QDir(document.rootPath).filePath("output/match_preview");
    const QString pointcloudDir = QDir(document.rootPath).filePath("output/pointcloud");
    const QString poiDir = QDir(document.rootPath).filePath("output/poi");

    // 第三步：逐对处理双目图像，依次执行提线、匹配、重建和导出。

    data_model::Point3DList mergedPoints;
    for (int i = 0; i < session.manifestRows.size(); ++i) {
        const data_model::StereoFrameManifestRow& row = session.manifestRows[i];
        data_model::StereoFrameRunResult frameResult;
        frameResult.manifestRow = row;

        const cv::Mat leftImage = cv::imread(row.leftImagePath.toStdString(), cv::IMREAD_COLOR);
        const cv::Mat rightImage = cv::imread(row.rightImagePath.toStdString(), cv::IMREAD_COLOR);
        if (leftImage.empty() || rightImage.empty()) {
            frameResult.message = QString("无法读取双目图像: %1 | %2").arg(row.leftImagePath, row.rightImagePath);
            session.failedFrameIndices.append(i);
            session.frameResults.append(frameResult);
            logs.append(frameResult.message);
            continue;
        }

        // 根据当前配置选择中心线算法，并分别处理左右图像。

        auto extractor = algorithms::CenterlineExtractorFactory::create(session.config.centerlineMethod);
        const cv::Rect leftRoi = roiFromValues(session.config.leftRoi, leftImage.size(), session.config.useFullLeftRoi);
        const cv::Rect rightRoi = roiFromValues(session.config.rightRoi, rightImage.size(), session.config.useFullRightRoi);
        const algorithms::CenterlineExtractionOutput leftCenterline = extractor->extract(leftImage, session.config, leftRoi);
        const algorithms::CenterlineExtractionOutput rightCenterline = extractor->extract(rightImage, session.config, rightRoi);
        frameResult.leftCenterline.points = leftCenterline.points;
        frameResult.rightCenterline.points = rightCenterline.points;
        logs.append(leftCenterline.diagnosticMessage);
        logs.append(rightCenterline.diagnosticMessage);

        // 基于左右中心线和双目标定进行点匹配。

        const StereoMatchingOutput matching = matcher_.match(
            leftImage,
            rightImage,
            row,
            session.config,
            calibration,
            leftCenterline,
            rightCenterline);
        frameResult.stereoMatch.pairCount = static_cast<int>(matching.pairs.size());
        logs.append(matching.diagnosticMessage);

        // 将匹配结果恢复为三维点云。

        const ReconstructionFrameOutput reconstruction = reconstructionEngine_.reconstruct(
            leftImage,
            rightImage,
            row,
            session.config,
            calibration,
            matching);
        frameResult.pointCloud.points = reconstruction.points;
        frameResult.message = reconstruction.message;

        const QString frameName = frameBaseName(row);
        frameResult.pointCloud.frameName = frameName;
        frameResult.leftCenterline.sourceImagePath = row.leftImagePath;
        frameResult.rightCenterline.sourceImagePath = row.rightImagePath;
        frameResult.leftCenterline.csvPath = QDir(centerlineCsvDir).filePath(frameName + "_left_centerline.csv");
        frameResult.rightCenterline.csvPath = QDir(centerlineCsvDir).filePath(frameName + "_right_centerline.csv");
        frameResult.leftCenterline.previewPath = QDir(centerlinePreviewDir).filePath(frameName + "_left_preview.png");
        frameResult.rightCenterline.previewPath = QDir(centerlinePreviewDir).filePath(frameName + "_right_preview.png");
        frameResult.stereoMatch.csvPath = QDir(matchCsvDir).filePath(frameName + "_matches.csv");
        frameResult.stereoMatch.previewPath = QDir(matchPreviewDir).filePath(frameName + "_matches.png");
        frameResult.pointCloud.plyPath = QDir(pointcloudDir).filePath(frameName + ".ply");
        frameResult.pointCloud.poiPath = QDir(poiDir).filePath(frameName + "_poi3d.csv");

        // 将当前帧的调试数据和重建结果全部落盘。

        QString exportError;
        const bool leftCsvOk = resultExporter_.exportCenterlineCsv(frameResult.leftCenterline.points, frameResult.leftCenterline.csvPath, exportError);
        const bool rightCsvOk = leftCsvOk && resultExporter_.exportCenterlineCsv(frameResult.rightCenterline.points, frameResult.rightCenterline.csvPath, exportError);
        const bool leftPreviewOk = rightCsvOk && resultExporter_.exportPreviewImage(leftCenterline.previewImage, frameResult.leftCenterline.previewPath, exportError);
        const bool rightPreviewOk = leftPreviewOk && resultExporter_.exportPreviewImage(rightCenterline.previewImage, frameResult.rightCenterline.previewPath, exportError);
        const bool matchCsvOk = rightPreviewOk && resultExporter_.exportMatchCsv(matching.pairs, frameResult.stereoMatch.csvPath, exportError);
        const bool matchPreviewOk = matchCsvOk && resultExporter_.exportPreviewImage(matching.previewImage, frameResult.stereoMatch.previewPath, exportError);
        const bool plyOk = matchPreviewOk && resultExporter_.exportPointCloudPly(frameResult.pointCloud.points, frameResult.pointCloud.plyPath, exportError);
        const bool poiOk = plyOk && resultExporter_.exportPointCloudAsPoi3D(frameResult.pointCloud.points, frameResult.pointCloud.poiPath, exportError);

        if (!leftCsvOk || !rightCsvOk || !leftPreviewOk || !rightPreviewOk || !matchCsvOk || !matchPreviewOk || !plyOk || !poiOk) {
            frameResult.message = exportError;
            session.failedFrameIndices.append(i);
            logs.append(exportError);
        } else {
            frameResult.succeeded = true;
            mergedPoints.insert(mergedPoints.end(), frameResult.pointCloud.points.begin(), frameResult.pointCloud.points.end());
            logs.append(QString("%1: %2").arg(frameName, frameResult.message));
        }

        session.frameResults.append(frameResult);
    }

    // 第四步：汇总所有成功帧的点云并导出合并结果。

    session.mergedPointCloudPath = QDir(pointcloudDir).filePath("merged_session.ply");
    session.mergedPoiPath = QDir(poiDir).filePath("merged_session_poi3d.csv");
    QString mergedError;
    if (!mergedPoints.empty()) {
        resultExporter_.exportPointCloudPly(mergedPoints, session.mergedPointCloudPath, mergedError);
        resultExporter_.exportPointCloudAsPoi3D(mergedPoints, session.mergedPoiPath, mergedError);
    }

    return !session.frameResults.isEmpty();
}

}  // namespace htmsr::reconstruction_core
