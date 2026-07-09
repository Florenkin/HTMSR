#include "app/services/AppConfigService.h"

#include <QSettings>
#include <QString>

namespace htmsr::app {
namespace {

std::string readString(QSettings& settings, const char* key, const std::string& fallback = {})
{
    return settings.value(key, QString::fromStdString(fallback)).toString().toStdString();
}

void writeString(QSettings& settings, const char* key, const std::string& value)
{
    settings.setValue(key, QString::fromStdString(value));
}

} // namespace

AppProjectConfig AppConfigService::load() const
{
    QSettings settings("HTMSR", "HTMSR");
    AppProjectConfig config;
    config.leftCalibrationDirectory = readString(settings, "paths/leftCalibration");
    config.rightCalibrationDirectory = readString(settings, "paths/rightCalibration");
    config.leftReconstructionDirectory = readString(settings, "paths/leftReconstruction");
    config.rightReconstructionDirectory = readString(settings, "paths/rightReconstruction");
    config.calibrationFile = readString(settings, "paths/calibrationFile", "stereo_calibration.yml");
    config.outputDirectory = readString(settings, "paths/outputDirectory", ".");
    config.calibrationInput.leftDirectory = config.leftCalibrationDirectory;
    config.calibrationInput.rightDirectory = config.rightCalibrationDirectory;
    config.calibrationInput.outputFile = config.calibrationFile;
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
    config.laserConfig.laserColor = static_cast<LaserColor>(settings.value("reconstruction/color", static_cast<int>(LaserColor::Blue)).toInt());
    config.laserConfig.leftRoi = cv::Rect(
        settings.value("reconstruction/leftRoiX", 0).toInt(),
        settings.value("reconstruction/leftRoiY", 0).toInt(),
        settings.value("reconstruction/leftRoiW", 3072).toInt(),
        settings.value("reconstruction/leftRoiH", 2048).toInt());
    config.laserConfig.rightRoi = cv::Rect(
        settings.value("reconstruction/rightRoiX", 0).toInt(),
        settings.value("reconstruction/rightRoiY", 0).toInt(),
        settings.value("reconstruction/rightRoiW", 3072).toInt(),
        settings.value("reconstruction/rightRoiH", 2048).toInt());
    config.laserConfig.grayThreshold = settings.value("reconstruction/grayThreshold", 150).toInt();
    config.laserConfig.minGray = settings.value("reconstruction/minGray", 30).toInt();
    config.laserConfig.binaryThreshold = settings.value("reconstruction/binaryThreshold", 100.0).toDouble();
    config.laserConfig.selectionThreshold = settings.value("reconstruction/selectionThreshold", 200.0).toDouble();
    config.laserConfig.stripeWidth = settings.value("reconstruction/stripeWidth", 5.0).toDouble();
    config.laserConfig.removeEndPoints = settings.value("reconstruction/removeEndpoints", false).toBool();
    config.laserConfig.removeEndPointCount = settings.value("reconstruction/removeEndpointCount", 10).toInt();
    config.matchDistanceThreshold = settings.value("reconstruction/matchDistance", 0.1).toDouble();
    return config;
}

void AppConfigService::save(const AppProjectConfig& config) const
{
    QSettings settings("HTMSR", "HTMSR");
    writeString(settings, "paths/leftCalibration", config.leftCalibrationDirectory);
    writeString(settings, "paths/rightCalibration", config.rightCalibrationDirectory);
    writeString(settings, "paths/leftReconstruction", config.leftReconstructionDirectory);
    writeString(settings, "paths/rightReconstruction", config.rightReconstructionDirectory);
    writeString(settings, "paths/calibrationFile", config.calibrationFile);
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
