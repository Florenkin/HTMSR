#include "app/acquisition/GalvoCaptureSupport.h"
#include "app/acquisition/GalvoController.h"
#include "app/ui/AcquisitionPanel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QLineEdit>
#include <QPixmap>
#include <QSpinBox>
#include <QTabWidget>

#include <iostream>
#include <stdexcept>

using namespace htmsr::app;

void runAppConfigPersistenceTests();
void verifyRestoredSettings(const QString& settingsRoot);
void runCalibrationWorkflowTests();
void runReconstructionStorageTests();
void runResultExportTests();

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try {
        const auto startupArguments = application.arguments();
        if (startupArguments.size() == 3 && startupArguments[1] == "--verify-restored-settings") {
            verifyRestoredSettings(startupArguments[2]);
            std::cout << "PASS: saved parameters restored in a new process\n";
            return 0;
        }
        AcquisitionPanel panel;
        auto* portDisplay = panel.findChild<QLineEdit*>("galvoSerialPort");
        require(portDisplay != nullptr && portDisplay->isReadOnly(), "The hardware panel must display the automatically detected port");
        panel.setSerialPorts({});
        require(panel.integratedScanConfig().galvo.portName.empty(), "No detected port must not fabricate COM3");
        require(portDisplay->text().isEmpty() && portDisplay->placeholderText().isEmpty(),
            "Missing USB serial device must leave the port display empty without placeholder text");
        panel.setSerialPorts({"COM8", "COM9"});
        require(panel.integratedScanConfig().galvo.portName.empty(), "Ambiguous ports must not silently select another device");
        panel.setSerialPorts({"COM8"});
        require(panel.integratedScanConfig().galvo.portName == "COM8" && portDisplay->text() == "COM8", "A newly connected single port must automatically be displayed and used");
        panel.setSerialPorts({"COM8", "COM9"});
        require(panel.integratedScanConfig().galvo.portName == "COM8", "The recognized device must remain selected while it is still online");
        panel.setSerialPorts({"COM11"});
        require(panel.integratedScanConfig().galvo.portName == "COM11" && portDisplay->text() == "COM11", "Reconnect must replace a stale port with the new online port");
        std::cout << "PASS: automatic serial recognition, missing USB device, ambiguity and reconnect\n";

        auto* speed = panel.findChild<QSpinBox*>("galvoSpeed");
        require(speed != nullptr, "The shared speed control must be available");
        speed->setValue(55);
        const auto captureConfig = panel.integratedScanConfig();
        require(captureConfig.galvo.forwardSpeedMs == 55 && captureConfig.galvo.reverseSpeedMs == 55,
            "One speed change must configure both scan directions");
        require(captureConfig.galvo.laserDuty == 100 && captureConfig.stereoCamera.leftParameters.gain == 15.0 &&
            captureConfig.stereoCamera.rightParameters.gain == 15.0, "Hidden controls must retain the fixed laser duty and camera gain");
        panel.setReconstructionDirectories("capture/current/left", "capture/current/right");
        const auto reconstruction = panel.reconstructionInput({});
        const auto project = panel.projectConfig();
        require(reconstruction.leftDirectory == "capture/current/left" && reconstruction.rightDirectory == "capture/current/right" &&
            project.leftReconstructionDirectory == reconstruction.leftDirectory && project.rightReconstructionDirectory == reconstruction.rightDirectory,
            "The selected reconstruction directories must supply both stereo inputs");
        std::cout << "PASS: shared scan speed, fixed gain/duty and independent reconstruction directories\n";

        const auto arguments = application.arguments();
        const int renderIndex = arguments.indexOf("--render-preview");
        if (renderIndex >= 0 && renderIndex + 1 < arguments.size()) {
            speed->setValue(30);
            CameraDeviceInfo left;
            left.id = "USB3:DA7866275";
            left.name = "RightCamera";
            left.transportType = "USB3";
            left.serialNumber = "DA7866275";
            CameraDeviceInfo right = left;
            right.id = "USB3:DA7866280";
            right.name = "LeftCamera";
            right.serialNumber = "DA7866280";
            panel.setDevices({left, right});
            panel.setSerialPorts({"COM11"});
            panel.setReconstructionDirectories({}, {});
            auto* tabs = panel.findChild<QTabWidget*>();
            require(tabs != nullptr, "Workflow tabs must be present for the preview");
            tabs->setCurrentIndex(arguments.contains("--calibration-tab") ? 0 : 1);
            application.setFont(QFont("Microsoft YaHei", 10));
            panel.rawGalvoCommandWidget()->hide();
            panel.resize(580, 1100);
            panel.show();
            application.processEvents();
            const QString imagePath = arguments[renderIndex + 1];
            QDir().mkpath(QFileInfo(imagePath).absolutePath());
            require(panel.grab().save(imagePath), "UI review image must be saved");
            std::cout << "Preview saved: " << imagePath.toStdString() << '\n';
            return 0;
        }

        GalvoScanConfig config;
        config.portName.clear();
        SerialGalvoController empty(config);
        require(!empty.connect() && !empty.lastError().empty(), "No port must produce an actionable error");
        config.portName = "COM2147483647"; // Deliberately nonexistent: never contact real hardware.
        SerialGalvoController missing(config);
        require(!missing.connect() && !missing.isConnected(), "A nonexistent device must remain disconnected");
        const auto detail = missing.lastError();
        require(detail.find("COM2147483647") != std::string::npos &&
            detail.find("Windows") != std::string::npos, "Native failure must retain device name and Windows error");
        missing.disconnect();
        require(missing.lastError() == detail, "Cleanup must not erase the diagnostic");
        bool rejected = false;
        try {
            configureAndVerifyGalvoMotion(missing, config, [](int) {});
        } catch (const std::exception& ex) {
            rejected = true;
            require(std::string(ex.what()) == missing.lastError(), "Preflight must display the native connection error");
        }
        require(rejected && !missing.isConnected(), "Connection failures must still block capture");
        std::cout << "PASS: native connection failure reaches preflight without contacting hardware\n";
        runAppConfigPersistenceTests();
        runCalibrationWorkflowTests();
        runReconstructionStorageTests();
        runResultExportTests();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << '\n';
        return 1;
    }
}
