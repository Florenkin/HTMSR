#include "app/services/AppConfigService.h"
#include "app/ui/AcquisitionPanel.h"

#include <QCoreApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace htmsr;
using namespace htmsr::app;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void isolateSettings(const QString& root)
{
    qputenv("HTMSR_CONFIG_DIR", QDir(root).filePath("config").toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root);
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "HTMSR", "HTMSR");
    require(settings.format() == QSettings::IniFormat &&
        QDir::cleanPath(settings.fileName()).startsWith(QDir::cleanPath(root) + '/'),
        "Persistence tests must never write the user's actual settings");
}
}

void verifyRestoredSettings(const QString& settingsRoot)
{
    isolateSettings(settingsRoot);
    const auto restored = AppConfigService{}.load();
    AcquisitionPanel reopened;
    int motionCommands = 0;
    QObject::connect(&reopened, &AcquisitionPanel::galvoMotionParametersChanged, [&]() { ++motionCommands; });
    reopened.setProjectConfig(restored);
    require(motionCommands == 0, "Loading saved parameters must not send hardware motion commands");
    const auto capture = reopened.integratedScanConfig();
    require(std::abs(capture.galvo.stepAngleDeg - 0.05) < 0.000001 &&
        std::abs(capture.totalRotationAngleDeg - 20.0) < 0.000001,
        "The next launch must restore the edited step and total rotation angles");
    require(capture.stereoCamera.frameCount == 400, "Frame count must be recomputed from restored angles");
    require(capture.galvo.forwardSpeedMs == 75 && capture.galvo.reverseSpeedMs == 75,
        "The next launch must restore the shared scan speed");
    require(capture.stereoCamera.leftParameters.exposureTime == 4500.0 &&
        capture.stereoCamera.rightParameters.exposureTime == 4500.0 &&
        !capture.stereoCamera.leftParameters.useHardwareTrigger &&
        !capture.stereoCamera.rightParameters.useHardwareTrigger &&
        capture.stereoCamera.leftParameters.triggerSourceLine == 3 &&
        capture.stereoCamera.rightParameters.triggerSourceLine == 3,
        "Exposure, capture mode and trigger line must survive a restart");
    require(!capture.verifiedGalvoMotion, "A saved setting must not replace fresh hardware verification");
    const auto project = reopened.projectConfig();
    require(project.leftReconstructionDirectory.empty() &&
        project.rightReconstructionDirectory.empty(), "The next launch must leave the reconstruction directory empty");
    require(project.calibrationInput.boardSize == cv::Size(9, 6) &&
        project.calibrationInput.squareSize == cv::Size2d(25.0, 25.0),
        "Saved calibration values equal to old defaults must not be rewritten");
    require(project.laserExtractionDirectory == "custom/激光线输出" && project.saveLaserExtractionImages,
        "Laser extraction output directory and enabled state must survive a restart");
    require(project.laserConfig.leftRoi == cv::Rect(350, 0, 1900, 2048) &&
        project.laserConfig.rightRoi == cv::Rect(900, 0, 1500, 2048) &&
        project.laserConfig.grayThreshold == 205,
        "Saved reconstruction parameters must not be replaced by defaults");
}

void runAppConfigPersistenceTests()
{
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "Temporary settings directory must be available");
    isolateSettings(settingsDirectory.path());
    {
        QSettings legacy(QSettings::defaultFormat(), QSettings::UserScope, "HTMSR", "HTMSR");
        legacy.setValue("paths/leftReconstruction", "old/capture/left");
        legacy.setValue("paths/rightReconstruction", "old/capture/right");
        legacy.sync();
    }
    AppConfigService service;
    const auto firstLaunch = service.load();
    require(firstLaunch.acquisitionParameters.stepAngleDeg == 0.02 &&
        firstLaunch.acquisitionParameters.totalRotationAngleDeg == 40.0,
        "First launch without saved settings must retain the defaults");
    require(firstLaunch.leftReconstructionDirectory.empty() && firstLaunch.rightReconstructionDirectory.empty(),
        "Legacy saved reconstruction paths must not populate the new empty directory");
    require(firstLaunch.laserExtractionDirectory == "output/LaserExtraction" &&
        !firstLaunch.saveLaserExtractionImages,
        "Laser extraction images must default to the project output folder and remain disabled");

    auto changed = firstLaunch;
    changed.acquisitionParameters.stepAngleDeg = 0.2;
    changed.acquisitionParameters.totalRotationAngleDeg = 20.0;
    changed.acquisitionParameters.speedMs = 75;
    changed.acquisitionParameters.exposureTime = 4500.0;
    changed.acquisitionParameters.useHardwareTrigger = false;
    changed.acquisitionParameters.triggerSourceLine = 3;
    changed.calibrationInput.boardSize = cv::Size(9, 6);
    changed.calibrationInput.squareSize = cv::Size2d(25.0, 25.0);
    changed.laserConfig.leftRoi = cv::Rect(350, 0, 1900, 2048);
    changed.laserConfig.rightRoi = cv::Rect(900, 0, 1500, 2048);
    changed.laserConfig.grayThreshold = 205;
    changed.laserExtractionDirectory = "custom/激光线输出";
    changed.saveLaserExtractionImages = true;
    AcquisitionPanel panel;
    panel.setProjectConfig(changed);
    auto* step = panel.findChild<QDoubleSpinBox*>("galvoStepAngle");
    require(step != nullptr, "Step angle input must be available");
    step->findChild<QLineEdit*>()->setText("0.05");
    panel.commitPendingEdits();
    require(std::abs(panel.projectConfig().acquisitionParameters.stepAngleDeg - 0.05) < 0.000001,
        "Closing while the angle is still being edited must save the last typed value");
    require(panel.findChild<QLineEdit*>("laserExtractionDirectory") != nullptr &&
        panel.findChild<QCheckBox*>("saveLaserExtractionImages") != nullptr,
        "Laser extraction output controls must be available in the reconstruction panel");
    service.save(panel.projectConfig());
    {
        QSettings cleaned(QSettings::defaultFormat(), QSettings::UserScope, "HTMSR", "HTMSR");
        require(cleaned.contains("paths/leftReconstruction") && cleaned.contains("paths/rightReconstruction"),
            "New configuration saving must leave legacy settings untouched");
    }

    QProcess nextLaunch;
    nextLaunch.start(QCoreApplication::applicationFilePath(),
        {"--verify-restored-settings", settingsDirectory.path()});
    require(nextLaunch.waitForStarted(5000) && nextLaunch.waitForFinished(15000),
        "A separate process must be able to read the saved configuration");
    const auto output = nextLaunch.readAllStandardOutput() + nextLaunch.readAllStandardError();
    if (nextLaunch.exitStatus() != QProcess::NormalExit || nextLaunch.exitCode() != 0) {
        throw std::runtime_error("Restart verification failed: " + output.toStdString());
    }
    std::cout << output.toStdString();
    std::cout << "PASS: last typed value persisted; fresh process restores angles, speed, exposure, mode, trigger and reconstruction parameters\n";
}
