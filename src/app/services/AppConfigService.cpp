#include "app/services/AppConfigService.h"
#include "app/services/ConfigFiles.h"
#include "core/Logger.h"

#include <QSettings>
#include <QStringList>
#include <memory>

namespace htmsr::app {
namespace {

QMap<QString, QVector<ConfigEntry>> definitions(const AppProjectConfig& config)
{
    const auto& acquisition = config.acquisitionParameters;
    QMap<QString, QVector<ConfigEntry>> result;
    auto add = [&](const QString& key, const QVariant& value, const QString& comment) {
        result[key.section('/', 0, 0) + ".ini"].append({key, value, comment});
    };
    add("paths/outputDirectory", QString::fromStdString(config.outputDirectory), QString::fromUtf8("采集和算法结果输出目录；相对路径以项目目录为基准。"));
    add("calibration/boardWidth", config.calibrationInput.boardSize.width, QString::fromUtf8("棋盘格横向内角点数量，范围 2～100。"));
    add("calibration/boardHeight", config.calibrationInput.boardSize.height, QString::fromUtf8("棋盘格纵向内角点数量，范围 2～100。"));
    add("calibration/squareWidth", config.calibrationInput.squareSize.width, QString::fromUtf8("棋盘格单格宽度，单位毫米，范围 0.001～10000。"));
    add("calibration/squareHeight", config.calibrationInput.squareSize.height, QString::fromUtf8("棋盘格单格高度，单位毫米，范围 0.001～10000。"));
    add("calibration/imageBegin", config.calibrationInput.imageRange.begin, QString::fromUtf8("标定图像起始索引；-1 表示不限制。"));
    add("calibration/imageEnd", config.calibrationInput.imageRange.end, QString::fromUtf8("标定图像结束索引；-1 表示不限制。"));
    add("reconstruction/mode", config.laserConfig.mode == LaserExtractionMode::Steger ? 1 : 0, QString::fromUtf8("中心线算法：0=灰度重心法，1=Steger。"));
    add("reconstruction/color", static_cast<int>(config.laserConfig.laserColor), QString::fromUtf8("激光颜色通道：0=红，1=绿，2=蓝，3=灰度。"));
    add("reconstruction/leftRoiX", config.laserConfig.leftRoi.x, QString::fromUtf8("左图处理区域起点横坐标，单位像素，范围 0～100000。"));
    add("reconstruction/leftRoiY", config.laserConfig.leftRoi.y, QString::fromUtf8("左图处理区域起点纵坐标，单位像素，范围 0～100000。"));
    add("reconstruction/leftRoiW", config.laserConfig.leftRoi.width, QString::fromUtf8("左图处理区域宽度，单位像素，范围 1～100000。"));
    add("reconstruction/leftRoiH", config.laserConfig.leftRoi.height, QString::fromUtf8("左图处理区域高度，单位像素，范围 1～100000。"));
    add("reconstruction/rightRoiX", config.laserConfig.rightRoi.x, QString::fromUtf8("右图处理区域起点横坐标，单位像素，范围 0～100000。"));
    add("reconstruction/rightRoiY", config.laserConfig.rightRoi.y, QString::fromUtf8("右图处理区域起点纵坐标，单位像素，范围 0～100000。"));
    add("reconstruction/rightRoiW", config.laserConfig.rightRoi.width, QString::fromUtf8("右图处理区域宽度，单位像素，范围 1～100000。"));
    add("reconstruction/rightRoiH", config.laserConfig.rightRoi.height, QString::fromUtf8("右图处理区域高度，单位像素，范围 1～100000。"));
    add("reconstruction/grayThreshold", config.laserConfig.grayThreshold, QString::fromUtf8("灰度重心法灰度阈值，范围 0～255。"));
    add("reconstruction/minGray", config.laserConfig.minGray, QString::fromUtf8("激光提取最小灰度，范围 0～255。"));
    add("reconstruction/binaryThreshold", config.laserConfig.binaryThreshold, QString::fromUtf8("Steger 二值化阈值，范围 0～255。"));
    add("reconstruction/selectionThreshold", config.laserConfig.selectionThreshold, QString::fromUtf8("Steger 筛选阈值，范围 0～255。"));
    add("reconstruction/stripeWidth", config.laserConfig.stripeWidth, QString::fromUtf8("预估激光条纹宽度，单位像素，范围 0.5～100。"));
    add("reconstruction/removeEndpoints", config.laserConfig.removeEndPoints, QString::fromUtf8("是否删除中心线端点：true=删除，false=保留。"));
    add("reconstruction/removeEndpointCount", config.laserConfig.removeEndPointCount, QString::fromUtf8("每端删除的点数，范围 0～10000。"));
    add("reconstruction/matchDistance", config.matchDistanceThreshold, QString::fromUtf8("双目匹配距离阈值，范围 0.0001～100。"));
    add("acquisition/exposureTime", acquisition.exposureTime, QString::fromUtf8("左右相机曝光时间，单位微秒，范围 1～10000000。"));
    add("acquisition/useHardwareTrigger", acquisition.useHardwareTrigger, QString::fromUtf8("重建采集模式：true=硬触发，false=软触发；标定和预览自由取流。"));
    add("acquisition/triggerSourceLine", acquisition.triggerSourceLine, QString::fromUtf8("相机外部触发线编号，范围 0～5。"));
    add("acquisition/stepAngleDeg", acquisition.stepAngleDeg, QString::fromUtf8("振镜步进角度，单位度，范围 0.01～650.25。"));
    add("acquisition/totalRotationAngleDeg", acquisition.totalRotationAngleDeg, QString::fromUtf8("总旋转角度，单位度，范围 0～40；设备协议按整数角度执行。"));
    add("acquisition/speedMs", acquisition.speedMs, QString::fromUtf8("正反方向共用扫描速度参数，单位毫秒，范围 1～1000。"));
    add("paths/restoreInputPaths", config.restoreInputPaths, QString::fromUtf8("启动时是否恢复输入目录及标定文件：true=恢复，false=保持为空。"));
    add("paths/leftCalibration", QString::fromStdString(config.leftCalibrationDirectory), QString::fromUtf8("左相机标定图像目录；可留空。"));
    add("paths/rightCalibration", QString::fromStdString(config.rightCalibrationDirectory), QString::fromUtf8("右相机标定图像目录；可留空。"));
    add("paths/leftReconstruction", QString::fromStdString(config.leftReconstructionDirectory), QString::fromUtf8("左相机重建图像目录；可留空。"));
    add("paths/rightReconstruction", QString::fromStdString(config.rightReconstructionDirectory), QString::fromUtf8("右相机重建图像目录；可留空。"));
    add("paths/calibrationFile", QString::fromStdString(config.calibrationFile), QString::fromUtf8("双目标定结果 YML/YAML 路径；可留空。"));
    add("paths/logDirectory", QStringLiteral("../log"), QString::fromUtf8("运行日志目录；相对路径以 config 目录为基准。"));
    add("reconstruction/filterStegerPoints", config.laserConfig.filterStegerPoints, QString::fromUtf8("是否筛选 Steger 中心线点：true=筛选，false=保留全部。"));
    add("reconstruction/imageBegin", config.reconstructionImageRange.begin, QString::fromUtf8("重建图像起始索引；-1 表示不限制。"));
    add("reconstruction/imageEnd", config.reconstructionImageRange.end, QString::fromUtf8("重建图像结束索引；-1 表示不限制。"));
    add("acquisition/cameraGain", acquisition.cameraGain, QString::fromUtf8("左右相机增益，默认 15，必须大于等于 0。"));
    add("acquisition/grabTimeoutMs", acquisition.grabTimeoutMs, QString::fromUtf8("单次取流超时，单位毫秒，必须大于 0。"));
    add("acquisition/baudRate", acquisition.baudRate, QString::fromUtf8("振镜串口波特率，默认 115200。"));
    add("acquisition/commandTimeoutMs", acquisition.commandTimeoutMs, QString::fromUtf8("振镜命令超时，单位毫秒，必须大于 0。"));
    add("acquisition/syncMode", acquisition.syncMode, QString::fromUtf8("控制器同步模式：true=同步，false=异步，与相机触发模式独立。"));
    add("acquisition/reverseDirection", acquisition.reverseDirection, QString::fromUtf8("扫描方向：false=正向，true=反向。"));
    add("acquisition/captureIntervalMs", acquisition.captureIntervalMs, QString::fromUtf8("振镜抓图间隔，单位毫秒，必须大于 0。"));
    add("acquisition/continuousCaptureWaitMs", acquisition.continuousCaptureWaitMs, QString::fromUtf8("连续采集等待时间，单位毫秒，必须大于 0。"));
    add("acquisition/laserDuty", acquisition.laserDuty, QString::fromUtf8("激光占空比，单位百分比，范围 0～100。"));
    add("acquisition/voltageRangeV", acquisition.voltageRangeV, QString::fromUtf8("振镜电压范围，单位伏，范围 0～10。"));
    return result;
}

class ProjectSettings {
public:
    ProjectSettings()
    {
        AppProjectConfig defaults;
        defaults.calibrationFile.clear();
        QSettings legacy(QSettings::defaultFormat(), QSettings::UserScope, "HTMSR", "HTMSR");
        auto groups = definitions(defaults);
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            auto file = std::make_shared<ConfigFile>(ConfigFiles::path(it.key()));
            if (!file->exists()) {
                // 只迁移旧版本实际使用的数值和输出路径，忽略已废弃的输入目录键。
                for (auto& entry : it.value()) {
                    if (!entry.key.startsWith("paths/") || entry.key == "paths/outputDirectory")
                        entry.value = legacy.value(entry.key, entry.value);
                }
                file->save(it.value());
            }
            files_.insert(it.key(), file);
        }
    }

    QVariant value(const QString& key, const QVariant& fallback) const
    {
        const auto file = files_.value(key.section('/', 0, 0) + ".ini");
        const auto result = file ? file->value(key, fallback) : fallback;
        static const QMap<QString, QPair<double, double>> limits = {
            {"calibration/boardWidth", {2, 100}}, {"calibration/boardHeight", {2, 100}},
            {"calibration/squareWidth", {0.001, 10000}}, {"calibration/squareHeight", {0.001, 10000}},
            {"calibration/imageBegin", {-1, 2147483647}}, {"calibration/imageEnd", {-1, 2147483647}},
            {"reconstruction/mode", {0, 1}}, {"reconstruction/color", {0, 3}},
            {"reconstruction/leftRoiX", {0, 100000}}, {"reconstruction/leftRoiY", {0, 100000}},
            {"reconstruction/leftRoiW", {1, 100000}}, {"reconstruction/leftRoiH", {1, 100000}},
            {"reconstruction/rightRoiX", {0, 100000}}, {"reconstruction/rightRoiY", {0, 100000}},
            {"reconstruction/rightRoiW", {1, 100000}}, {"reconstruction/rightRoiH", {1, 100000}},
            {"reconstruction/grayThreshold", {0, 255}}, {"reconstruction/minGray", {0, 255}},
            {"reconstruction/binaryThreshold", {0, 255}}, {"reconstruction/selectionThreshold", {0, 255}},
            {"reconstruction/stripeWidth", {0.5, 100}}, {"reconstruction/matchDistance", {0.0001, 100}},
            {"reconstruction/removeEndpointCount", {0, 10000}},
            {"reconstruction/imageBegin", {-1, 2147483647}}, {"reconstruction/imageEnd", {-1, 2147483647}},
            {"acquisition/exposureTime", {1, 10000000}}, {"acquisition/triggerSourceLine", {0, 5}},
            {"acquisition/stepAngleDeg", {0.01, 650.25}}, {"acquisition/totalRotationAngleDeg", {0, 40}},
            {"acquisition/speedMs", {1, 1000}}, {"acquisition/cameraGain", {0, 1000000}},
            {"acquisition/grabTimeoutMs", {1, 2147483647}}, {"acquisition/baudRate", {1, 2147483647}},
            {"acquisition/commandTimeoutMs", {1, 2147483647}}, {"acquisition/captureIntervalMs", {1, 65535}},
            {"acquisition/continuousCaptureWaitMs", {1, 65535}}, {"acquisition/laserDuty", {0, 100}},
            {"acquisition/voltageRangeV", {0, 10}}
        };
        const auto limit = limits.constFind(key);
        if (limit != limits.constEnd() && (result.toDouble() < limit->first || result.toDouble() > limit->second)) {
            warnings_.append(QString::fromUtf8("参数 %1 超出范围，使用默认值。").arg(key));
            return fallback;
        }
        return result;
    }

    QString errors() const
    {
        QStringList messages = warnings_;
        for (const auto& file : files_) if (!file->error().isEmpty()) messages.append(file->error());
        return messages.join('\n');
    }

private:
    QMap<QString, std::shared_ptr<ConfigFile>> files_;
    mutable QStringList warnings_;
};

std::string readString(ProjectSettings& settings, const char* key, const std::string& fallback = {})
{
    // 统一将 UTF-8 配置文本转换为 std::string。
    return settings.value(key, QString::fromStdString(fallback)).toString().toStdString();
}

cv::Rect readRoi(
    ProjectSettings& settings,
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
    ProjectSettings settings;
    lastError_.clear();
    AppProjectConfig config;
    config.restoreInputPaths = settings.value("paths/restoreInputPaths", true).toBool();
    if (config.restoreInputPaths) {
        config.leftCalibrationDirectory = readString(settings, "paths/leftCalibration");
        config.rightCalibrationDirectory = readString(settings, "paths/rightCalibration");
        config.leftReconstructionDirectory = readString(settings, "paths/leftReconstruction");
        config.rightReconstructionDirectory = readString(settings, "paths/rightReconstruction");
        config.calibrationFile = readString(settings, "paths/calibrationFile");
    } else {
        config.calibrationFile.clear();
    }
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
    acquisition.cameraGain = settings.value("acquisition/cameraGain", acquisition.cameraGain).toDouble();
    acquisition.grabTimeoutMs = settings.value("acquisition/grabTimeoutMs", acquisition.grabTimeoutMs).toInt();
    acquisition.baudRate = settings.value("acquisition/baudRate", acquisition.baudRate).toInt();
    acquisition.commandTimeoutMs = settings.value("acquisition/commandTimeoutMs", acquisition.commandTimeoutMs).toInt();
    acquisition.syncMode = settings.value("acquisition/syncMode", acquisition.syncMode).toBool();
    acquisition.reverseDirection = settings.value("acquisition/reverseDirection", acquisition.reverseDirection).toBool();
    acquisition.captureIntervalMs = settings.value("acquisition/captureIntervalMs", acquisition.captureIntervalMs).toInt();
    acquisition.continuousCaptureWaitMs = settings.value("acquisition/continuousCaptureWaitMs", acquisition.continuousCaptureWaitMs).toInt();
    acquisition.laserDuty = settings.value("acquisition/laserDuty", acquisition.laserDuty).toInt();
    acquisition.voltageRangeV = settings.value("acquisition/voltageRangeV", acquisition.voltageRangeV).toDouble();
    config.laserConfig.filterStegerPoints = settings.value("reconstruction/filterStegerPoints", true).toBool();
    config.reconstructionImageRange = {settings.value("reconstruction/imageBegin", -1).toInt(), settings.value("reconstruction/imageEnd", -1).toInt()};
    lastError_ = settings.errors();
    if (!lastError_.isEmpty()) Logger::instance().warning("Config", lastError_.toStdString());
    return config;
}

bool AppConfigService::save(const AppProjectConfig& config) const
{
    lastError_.clear();
    const auto groups = definitions(config);
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        auto entries = it.value();
        ConfigFile file(ConfigFiles::path(it.key()));
        // 日志路径没有对应的界面控件，保留手工配置。
        for (auto& entry : entries) if (entry.key == "paths/logDirectory")
            entry.value = file.value(entry.key, entry.value);
        if (!file.save(entries)) {
            if (!lastError_.isEmpty()) lastError_.append('\n');
            lastError_.append(file.error());
        }
    }
    if (!lastError_.isEmpty()) Logger::instance().error("Config", lastError_.toStdString());
    return lastError_.isEmpty();
}

QString AppConfigService::lastError() const { return lastError_; }

} // namespace htmsr::app
