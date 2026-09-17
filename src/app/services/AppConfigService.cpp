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

} // namespace

AppProjectConfig AppConfigService::load() const
{
    // 组织名和应用名固定为 HTMSR，Windows 下会保存到注册表或 Qt 默认配置位置。
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "HTMSR", "HTMSR");
    AppProjectConfig config;
    config.leftCalibrationDirectory.clear();
    config.rightCalibrationDirectory.clear();
    config.leftReconstructionDirectory.clear();
    config.rightReconstructionDirectory.clear();
    config.calibrationFile.clear();
    config.outputDirectory = readString(settings, "paths/outputDirectory", "output");
    config.calibrationInput.leftDirectory = config.leftCalibrationDirectory;
    config.calibrationInput.rightDirectory = config.rightCalibrationDirectory;
    config.calibrationInput.outputFile = config.calibrationFile;
    // 已保存的值直接恢复，只有缺少配置项时才使用默认值。
    config.calibrationInput.boardSize = cv::Size(
        settings.value("calibration/boardWidth", 11).toInt(),
        settings.value("calibration/boardHeight", 8).toInt());
    config.calibrationInput.squareSize = cv::Size2d(
        settings.value("calibration/squareWidth", 15.0).toDouble(),
        settings.value("calibration/squareHeight", 15.0).toDouble());
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
    config.laserConfig.grayThreshold = settings.value("reconstruction/grayThreshold", 120).toInt();
    config.laserConfig.minGray = settings.value("reconstruction/minGray", 20).toInt();
    config.laserConfig.binaryThreshold = settings.value("reconstruction/binaryThreshold", 100.0).toDouble();
    config.laserConfig.selectionThreshold = settings.value("reconstruction/selectionThreshold", 200.0).toDouble();
    config.laserConfig.stripeWidth = settings.value("reconstruction/stripeWidth", 5.0).toDouble();
    config.laserConfig.removeEndPoints = settings.value("reconstruction/removeEndpoints", false).toBool();
    config.laserConfig.removeEndPointCount = settings.value("reconstruction/removeEndpointCount", 10).toInt();
    config.matchDistanceThreshold = settings.value("reconstruction/matchDistance", 0.5).toDouble();
    auto& acquisition = config.acquisitionParameters;
    acquisition.exposureTime = settings.value("acquisition/exposureTime", acquisition.exposureTime).toDouble();
    acquisition.useHardwareTrigger = settings.value("acquisition/useHardwareTrigger", acquisition.useHardwareTrigger).toBool();
    acquisition.triggerSourceLine = settings.value("acquisition/triggerSourceLine", acquisition.triggerSourceLine).toInt();
    acquisition.stepAngleDeg = settings.value("acquisition/stepAngleDeg", acquisition.stepAngleDeg).toDouble();
    acquisition.totalRotationAngleDeg = settings.value("acquisition/totalRotationAngleDeg", acquisition.totalRotationAngleDeg).toDouble();
    acquisition.speedMs = settings.value("acquisition/speedMs", acquisition.speedMs).toInt();
    return config;
}

void AppConfigService::save(const AppProjectConfig& config) const
{
    // 保存路径、标定参数和重建参数，便于下次启动直接恢复工作现场。
    // 标定和重建输入路径每次启动保持为空，由用户选择或在线采集后回填。
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "HTMSR", "HTMSR");
    settings.remove("paths/leftCalibration");
    settings.remove("paths/rightCalibration");
    settings.remove("paths/leftReconstruction");
    settings.remove("paths/rightReconstruction");
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
    const auto& acquisition = config.acquisitionParameters;
    settings.setValue("acquisition/exposureTime", acquisition.exposureTime);
    settings.setValue("acquisition/useHardwareTrigger", acquisition.useHardwareTrigger);
    settings.setValue("acquisition/triggerSourceLine", acquisition.triggerSourceLine);
    settings.setValue("acquisition/stepAngleDeg", acquisition.stepAngleDeg);
    settings.setValue("acquisition/totalRotationAngleDeg", acquisition.totalRotationAngleDeg);
    settings.setValue("acquisition/speedMs", acquisition.speedMs);
    settings.sync();
}

} // namespace htmsr::app
