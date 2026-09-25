#include "app/services/AppConfigService.h"
#include "app/services/ConfigAutoSave.h"
#include "app/services/ConfigFiles.h"
#include "app/ui/AcquisitionPanel.h"
#include "app/ui/SerialCommandPackWidget.h"
#include "QtTestApplication.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <cmath>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

using namespace htmsr;
using namespace htmsr::app;
void runAppConfigPersistenceTests();
void verifyRestoredSettings(const QString& settingsRoot);

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void write(const QString& path, const QByteArray& text)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Fixture must be writable");
    require(file.write(text) == text.size(), "Fixture must be completely written");
}
QByteArray read(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Configuration must be readable");
    return file.readAll();
}
void waitForSave(const std::function<bool()>& saved)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 3000) {
        QApplication::processEvents();
        if (saved()) return;
        QThread::msleep(1);
    }
}
}

int main(int argc, char** argv)
{
    htmsr::test::configureQtTestApplication(argv[0]);
    QApplication app(argc, argv);
    try {
        if (app.arguments().size() == 3 && app.arguments()[1] == "--verify-restored-settings") {
            verifyRestoredSettings(app.arguments()[2]);
            return 0;
        }
        // Existing restart checks also isolate the legacy settings and new config.
        runAppConfigPersistenceTests();
        QTemporaryDir root;
        require(root.isValid(), "Temporary configuration root must be valid");
        qputenv("HTMSR_CONFIG_DIR", root.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root.path());
        {
            QSettings legacy(QSettings::defaultFormat(), QSettings::UserScope, "HTMSR", "HTMSR");
            legacy.setValue("calibration/boardWidth", 12);
            legacy.setValue("acquisition/speedMs", 42);
            legacy.sync();
        }
        const auto portableExe = QDir(root.path()).filePath("portable");
        QDir().mkpath(portableExe + "/config");
        require(ConfigFiles::selectDirectory(portableExe, "development") == portableExe + "/config",
            "Adjacent config must take precedence over compiled development path");
        require(ConfigFiles::selectDirectory(root.path() + "/missing", root.path()) == root.path(),
            "Development config must remain the fallback");
        QDir().mkpath(root.path() + "/defaults");
        write(root.path() + "/defaults/example.ini", "[test]\nvalue=default\n");
        require(read(ConfigFiles::path("example.ini")).contains("default"), "Missing local file must copy its template");
        write(root.path() + "/example.ini", "manual");
        require(read(ConfigFiles::path("example.ini")) == "manual", "Templates must never overwrite local edits");
        AppConfigService service;
        auto project = service.load();
        require(project.calibrationInput.boardSize.width == 12 && project.acquisitionParameters.speedMs == 42,
            "Missing files must migrate saved legacy numbers");
        for (const auto& name : {"paths.ini", "calibration.ini", "reconstruction.ini", "acquisition.ini"}) {
            const auto bytes = read(ConfigFiles::path(name));
            require(bytes.contains("; ") && bytes == QString::fromUtf8(bytes).toUtf8(),
                "Each generated category must retain UTF-8 comments");
        }
        auto bytes = read(ConfigFiles::path("calibration.ini"));
        bytes.replace("boardWidth=12", "boardWidth=13");
        bytes.append(QString::fromUtf8("\n; 我的中文说明\ncustomValue=保留\n").toUtf8());
        write(ConfigFiles::path("calibration.ini"), bytes);
        project = service.load();
        require(project.calibrationInput.boardSize.width == 13, "Manual edits must override the old registry");
        project.acquisitionParameters.cameraGain = 8;
        project.acquisitionParameters.laserDuty = 60;
        project.acquisitionParameters.grabTimeoutMs = 1900;
        project.laserConfig.filterStegerPoints = false;
        project.calibrationInput.imageRange = {1, 3};
        project.reconstructionImageRange = {2, 4};
        project.leftReconstructionDirectory = "manual/left";
        project.rightReconstructionDirectory = "manual/right";
        project.laserExtractionDirectory = "manual/激光线";
        project.saveLaserExtractionImages = true;
        AcquisitionPanel panel;
        int changes = 0;
        QObject::connect(&panel, &AcquisitionPanel::projectConfigChanged, [&]() { ++changes; });
        panel.setProjectConfig(project);
        require(changes == 0, "Restoring configuration must not schedule an automatic save or hardware command");
        const auto scan = panel.integratedScanConfig();
        require(scan.stereoCamera.leftParameters.gain == 8 && scan.galvo.laserDuty == 60 &&
            scan.stereoCamera.rightParameters.grabTimeoutMs == 1900,
            "File-only acquisition options must affect the actual scan configuration");
        require(panel.calibrationInput().imageRange.begin == 1 && panel.reconstructionInput({}).imageRange.end == 4 &&
            !panel.reconstructionInput({}).laserConfig.filterStegerPoints,
            "File-only reconstruction and calibration options must reach the algorithms");
        auto* laserDirectory = panel.findChild<QLineEdit*>("laserExtractionDirectory");
        auto* saveLaserImages = panel.findChild<QCheckBox*>("saveLaserExtractionImages");
        require(laserDirectory && laserDirectory->text() == QString::fromUtf8("manual/激光线") &&
            saveLaserImages && saveLaserImages->isChecked(),
            "Laser extraction directory and save option must restore into the reconstruction panel");
        int saves = 0;
        ConfigAutoSave autosave([&]() { ++saves; require(service.save(panel.projectConfig()), "Automatic save must succeed"); }, nullptr, 20);
        QObject::connect(&panel, &AcquisitionPanel::projectConfigChanged, &autosave, [&]() { autosave.schedule(); });
        auto* step = panel.findChild<QDoubleSpinBox*>("galvoStepAngle");
        require(step != nullptr, "Editable scan step must exist");
        step->setValue(0.1);
        step->setValue(0.2);
        waitForSave([&]() { return saves > 0; });
        const auto savedStep = service.load().acquisitionParameters.stepAngleDeg;
        if (saves != 1 || std::abs(savedStep - 0.2) > 0.000001)
            std::cerr << "Autosave result: saves=" << saves << ", step=" << savedStep << '\n';
        require(saves == 1 && std::abs(savedStep - 0.2) < 0.000001,
            "Repeated UI edits must coalesce into a persisted save");
        const auto preserved = read(ConfigFiles::path("calibration.ini"));
        require(preserved.contains(QString::fromUtf8("我的中文说明").toUtf8()) && preserved.contains("customValue="),
            "Saving must preserve custom comments and unknown parameters");
        require(service.load().leftReconstructionDirectory == "manual/left", "Input paths must survive restart when enabled");
        require(service.load().laserExtractionDirectory == "manual/激光线" &&
            service.load().saveLaserExtractionImages,
            "Laser extraction output settings must survive restart");
        step->findChild<QLineEdit*>()->setText("0.05");
        panel.commitPendingEdits();
        autosave.flush();
        require(service.load().acquisitionParameters.stepAngleDeg == 0.05, "Closing must save the last pending typed number");
        project = panel.projectConfig();
        project.restoreInputPaths = false;
        require(service.save(project) && service.load().leftReconstructionDirectory.empty(),
            "The startup path restoration switch must be respected");
        require(service.load().laserExtractionDirectory == "manual/激光线",
            "Laser extraction output directory must not be cleared by input path restoration settings");

        write(ConfigFiles::path("environment.ini"), "[runtime]\nqtPluginDirectory=plugins\nextraDllDirectories=libs\n");
        require(ConfigFiles::environmentPath("runtime/qtPluginDirectory") == QDir(root.path()).filePath("plugins"),
            "Startup environment paths must resolve relative to config");
        const auto environment = read(ConfigFiles::path("environment.ini"));
        require(service.save(project) && read(ConfigFiles::path("environment.ini")) == environment,
            "UI saves must not change dependency environment paths");

        bytes = read(ConfigFiles::path("acquisition.ini"));
        bytes.replace("exposureTime=3000", "exposureTime=not-a-number");
        write(ConfigFiles::path("acquisition.ini"), bytes);
        require(service.load().acquisitionParameters.exposureTime == 3000 && !service.lastError().isEmpty(),
            "Malformed numbers must use a default and report a configuration warning");
        bytes.replace("exposureTime=not-a-number", "exposureTime=3000");
        bytes.replace("cameraGain=8", "cameraGain=-1");
        write(ConfigFiles::path("acquisition.ini"), bytes);
        require(service.load().acquisitionParameters.cameraGain == 15 && !service.lastError().isEmpty(),
            "Out-of-range parameters must report a warning and use safe defaults");
        const QByteArray broken("[calibration]\nboardWidth=11\nboardWidth=12\n");
        write(ConfigFiles::path("calibration.ini"), broken);
        require(!service.save(project) && read(ConfigFiles::path("calibration.ini")) == broken,
            "A syntax error must not destroy the original configuration");
        const auto blocked = ConfigFiles::path("blocked");
        write(blocked, "not a directory");
        ConfigFile cannotSave(blocked + "/settings.ini");
        require(!cannotSave.save({{"section/key", 1, QString::fromUtf8("测试参数")}}),
            "A failed save must be observable");
        {
            QSettings legacy(QSettings::defaultFormat(), QSettings::UserScope, "HTMSR", "HTMSR");
            require(legacy.format() == QSettings::IniFormat &&
                QDir::cleanPath(legacy.fileName()).startsWith(QDir::cleanPath(root.path()) + '/'),
                "Legacy command fixtures must never write the user's registry");
            legacy.setValue("serialCommandPacksInitialized", true);
            legacy.beginWriteArray("serialCommandPacks", 1);
            legacy.setArrayIndex(0);
            legacy.setValue("name", QString::fromUtf8("测试命令包"));
            legacy.setValue("content", "WAIT 10\n55 AA 01 1B 1B # comment");
            legacy.endArray();
            legacy.sync();
        }
        {
            SerialCommandPackWidget widget;
            const auto document = QJsonDocument::fromJson(read(ConfigFiles::path("command_packs.json")));
            require(document.isObject() && document.object().value("packs").toArray().size() == 1 &&
                document.object().value("packs").toArray()[0].toObject().value("name").toString() == QString::fromUtf8("测试命令包"),
                "Command packs must migrate into the classified config directory");
            require(read(ConfigFiles::path("command_packs.json")).contains(QString::fromUtf8("说明").toUtf8()),
                "Command pack parameters must have Chinese explanations");
        }
        {
            auto object = QJsonDocument::fromJson(read(ConfigFiles::path("command_packs.json"))).object();
            object.insert("_说明", QString::fromUtf8("手写总说明"));
            auto pack = object.value("packs").toArray()[0].toObject();
            pack.insert("_name说明", QString::fromUtf8("手写名称说明"));
            pack.insert("custom", "keep");
            object.insert("packs", QJsonArray{pack});
            write(ConfigFiles::path("command_packs.json"), QJsonDocument(object).toJson());
            SerialCommandPackWidget reopened;
            auto* editor = reopened.findChild<QTextEdit*>();
            require(editor != nullptr, "Command editor must be available");
            editor->setPlainText("WAIT 20\n55 AA 01 1B 1B");
            bool clickedSave = false;
            for (auto* button : reopened.findChildren<QPushButton*>()) {
                if (button->text() == QString::fromUtf8("保存")) { button->click(); clickedSave = true; break; }
            }
            require(clickedSave, "Command save action must be available");
            const auto saved = QJsonDocument::fromJson(read(ConfigFiles::path("command_packs.json"))).object();
            const auto savedPack = saved.value("packs").toArray()[0].toObject();
            require(saved.value("_说明").toString() == QString::fromUtf8("手写总说明") &&
                savedPack.value("_name说明").toString() == QString::fromUtf8("手写名称说明") &&
                savedPack.value("custom").toString() == "keep" && savedPack.value("content").toString().startsWith("WAIT 20"),
                "Saving commands must preserve manual annotations and unknown parameters");
        }
        write(ConfigFiles::path("command_packs.json"), "{\"initialized\":true,\"packs\":[]}");
        { SerialCommandPackWidget reopened; }
        require(QJsonDocument::fromJson(read(ConfigFiles::path("command_packs.json"))).object().value("packs").toArray().isEmpty(),
            "An intentionally empty command list must remain empty");
        std::cout << "PASS: classified UTF-8 config, legacy migration, manual edits, UI autosave, pending edits, startup paths, environment isolation, invalid values, atomic failure and command packs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
