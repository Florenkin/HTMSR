#include "app/services/AppConfigService.h"

#include <QSettings>
#include <QString>

namespace htmsr::app {
namespace {

std::string readString(QSettings& settings, const char* key, const std::string& fallback = {})
{
    // QSettings 使用 QVariant 保存，这里统一转换为 std::string。
    return settings.value(key, QString::fromStdString(fallback)).toString().toStdString();
}

void writeString(QSettings& settings, const char* key, const std::string& value)
{
    settings.setValue(key, QString::fromStdString(value));
}

cv::Rect readRoi(
    QSettings& settings,
    const char* xKey,
    const char* yKey,
    const char* widthKey,
    const char* heightKey,
    const cv::Rect& fallback)
{
    return cv::Rect(
        settings.value(xKey, fallback.x).toInt(),
        settings.value(yKey, fallback.y).toInt(),
        settings.value(widthKey, fallback.width).toInt(),
        settings.value(heightKey, fallback.height).toInt());
}

int readMigratedInt(QSettings& settings, const char* key, int fallback, int oldDefault, int newDefault)
{
    const int value = settings.value(key, fallback).toInt();
    return value == oldDefault ? newDefault : value;
}

double readMigratedDouble(QSettings& settings, const char* key, double fallback, double oldDefault, double newDefault)
{
    const double value = settings.value(key, fallback).toDouble();
    return value == oldDefault ? newDefault : value;
}

} // namespace

AppProjectConfig AppConfigService::load() const
{
    // 组织名和应用名固定为 HTMSR，Windows 下会保存到注册表或 Qt 默认配置位置。
    QSettings settings("HTMSR", "HTMSR");
    AppProjectConfig config;
    config.leftCalibrationDirectory.clear();
    config.rightCalibrationDirectory.clear();
    config.leftReconstructionDirectory = readString(settings, "paths/leftReconstruction");
    config.rightReconstructionDirectory = readString(settings, "paths/rightReconstruction");
    config.calibrationFile.clear();
    config.outputDirectory = readString(settings, "paths/outputDirectory", "output");
    config.calibrationInput.leftDirectory = config.leftCalibrationDirectory;
    config.calibrationInput.rightDirectory = config.rightCalibrationDirectory;
    config.calibrationInput.outputFile = config.calibrationFile;
    // 旧版本默认标定板为 9x6、25x25；加载到旧默认值时迁移到当前标定板参数。
    config.calibrationInput.boardSize = cv::Size(
        readMigratedInt(settings, "calibration/boardWidth", 11, 9, 11),
        readMigratedInt(settings, "calibration/boardHeight", 8, 6, 8));
    config.calibrationInput.squareSize = cv::Size2d(
        readMigratedDouble(settings, "calibration/squareWidth", 15.0, 25.0, 15.0),
        readMigratedDouble(settings, "calibration/squareHeight", 15.0, 25.0, 15.0));
    config.calibrationInput.imageRange = {
        settings.value("calibration/imageBegin", -1).toInt(),
        settings.value("calibration/imageEnd", -1).toInt()
    };
    config.laserConfig.mode = settings.value("reconstruction/mode", 0).toInt() == 1
        ? LaserExtractionMode::Steger
        : LaserExtractionMode::GrayCentroid;
    config.laserConfig.laserColor = static_cast<LaserColor>(settings.value("reconstruction/color", static_cast<int>(LaserColor::Gray)).toInt());
    const LaserExtractionConfig defaultLaserConfig;
    config.laserConfig.leftRoi = readRoi(
        settings,
        "reconstruction/leftRoiX",
        "reconstruction/leftRoiY",
        "reconstruction/leftRoiW",
        "reconstruction/leftRoiH",
        defaultLaserConfig.leftRoi);
    config.laserConfig.rightRoi = readRoi(
        settings,
        "reconstruction/rightRoiX",
        "reconstruction/rightRoiY",
        "reconstruction/rightRoiW",
        "reconstruction/rightRoiH",
        defaultLaserConfig.rightRoi);
    const cv::Rect oldNarrowDefaultRoi(2600, 0, 472, 2048);
    const cv::Rect previousLeftDefaultRoi(350, 0, 1900, 2048);
    const cv::Rect previousRightDefaultRoi(900, 0, 1500, 2048);
    // 仅迁移旧版本的窄 ROI 默认值，避免用户已经手动调整过的 ROI 被覆盖。
    if (config.laserConfig.leftRoi == oldNarrowDefaultRoi || config.laserConfig.leftRoi == previousLeftDefaultRoi) {
        config.laserConfig.leftRoi = defaultLaserConfig.leftRoi;
    }
    if (config.laserConfig.rightRoi == oldNarrowDefaultRoi || config.laserConfig.rightRoi == previousRightDefaultRoi) {
        config.laserConfig.rightRoi = defaultLaserConfig.rightRoi;
    }
    config.laserConfig.grayThreshold = settings.value("reconstruction/grayThreshold", 120).toInt();
    config.laserConfig.minGray = settings.value("reconstruction/minGray", 20).toInt();
    config.laserConfig.binaryThreshold = settings.value("reconstruction/binaryThreshold", 100.0).toDouble();
    config.laserConfig.selectionThreshold = settings.value("reconstruction/selectionThreshold", 200.0).toDouble();
    config.laserConfig.stripeWidth = settings.value("reconstruction/stripeWidth", 5.0).toDouble();
    config.laserConfig.removeEndPoints = settings.value("reconstruction/removeEndpoints", false).toBool();
    config.laserConfig.removeEndPointCount = settings.value("reconstruction/removeEndpointCount", 10).toInt();
    config.matchDistanceThreshold = settings.value("reconstruction/matchDistance", 0.5).toDouble();
    return config;
}

void AppConfigService::save(const AppProjectConfig& config) const
{
    // 保存路径、标定参数和重建参数，便于下次启动直接恢复工作现场。
    // 标定输入/输出路径每次启动保持为空，只在用户手动选择或在线采集后回填。
    QSettings settings("HTMSR", "HTMSR");
    settings.remove("paths/leftCalibration");
    settings.remove("paths/rightCalibration");
    writeString(settings, "paths/leftReconstruction", config.leftReconstructionDirectory);
    writeString(settings, "paths/rightReconstruction", config.rightReconstructionDirectory);
    settings.remove("paths/calibrationFile");
    writeString(settings, "paths/outputDirectory", config.outputDirectory);
    settings.setValue("calibration/boardWidth", config.calibrationInput.boardSize.width);
    settings.setValue("calibration/boardHeight", config.calibrationInput.boardSize.height);
    settings.setValue("calibration/squareWidth", config.calibrationInput.squareSize.width);
    settings.setValue("calibration/squareHeight", config.calibrationInput.squareSize.height);
    settings.setValue("calibration/imageBegin", config.calibrationInput.imageRange.begin);
    settings.setValue("calibration/imageEnd", config.calibrationInput.imageRange.end);
    settings.setValue("reconstruction/mode", config.laserConfig.mode == LaserExtractionMode::Steger ? 1 : 0);
    settings.setValue("reconstruction/color", static_cast<int>(config.laserConfig.laserColor));
    settings.setValue("reconstruction/leftRoiX", config.laserConfig.leftRoi.x);
    settings.setValue("reconstruction/leftRoiY", config.laserConfig.leftRoi.y);
    settings.setValue("reconstruction/leftRoiW", config.laserConfig.leftRoi.width);
    settings.setValue("reconstruction/leftRoiH", config.laserConfig.leftRoi.height);
    settings.setValue("reconstruction/rightRoiX", config.laserConfig.rightRoi.x);
    settings.setValue("reconstruction/rightRoiY", config.laserConfig.rightRoi.y);
    settings.setValue("reconstruction/rightRoiW", config.laserConfig.rightRoi.width);
    settings.setValue("reconstruction/rightRoiH", config.laserConfig.rightRoi.height);
    settings.setValue("reconstruction/grayThreshold", config.laserConfig.grayThreshold);
    settings.setValue("reconstruction/minGray", config.laserConfig.minGray);
    settings.setValue("reconstruction/binaryThreshold", config.laserConfig.binaryThreshold);
    settings.setValue("reconstruction/selectionThreshold", config.laserConfig.selectionThreshold);
    settings.setValue("reconstruction/stripeWidth", config.laserConfig.stripeWidth);
    settings.setValue("reconstruction/removeEndpoints", config.laserConfig.removeEndPoints);
    settings.setValue("reconstruction/removeEndpointCount", config.laserConfig.removeEndPointCount);
    settings.setValue("reconstruction/matchDistance", config.matchDistanceThreshold);
}

} // namespace htmsr::app
