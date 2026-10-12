#include "app/services/CalibrationCaptureSessionService.h"
#include "app/ui/AcquisitionPanel.h"
#include "core/CalibrationService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

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

cv::Mat chessboardImage(int pose, bool rightCamera)
{
    cv::Mat image(600, 800, CV_8UC3, cv::Scalar(220, 220, 220));
    const cv::Mat camera = (cv::Mat_<double>(3, 3) << 700, 0, 400, 0, 700, 300, 0, 0, 1);
    const cv::Vec3d rotation(0.08 * (pose - 2), 0.06 * (pose % 3 - 1), 0.015 * pose);
    const cv::Vec3d translation(-90 + 15 * (pose % 3) - (rightCamera ? 60 : 0),
        -65 + 10 * (pose % 2), 500 + 30 * pose);
    for (int y = -1; y < 6; ++y) {
        for (int x = -1; x < 9; ++x) {
            std::vector<cv::Point3f> square = {
                {25.0f * x, 25.0f * y, 0}, {25.0f * (x + 1), 25.0f * y, 0},
                {25.0f * (x + 1), 25.0f * (y + 1), 0}, {25.0f * x, 25.0f * (y + 1), 0}
            };
            std::vector<cv::Point2f> projected;
            cv::projectPoints(square, rotation, translation, camera, cv::Mat(), projected);
            std::vector<cv::Point> polygon;
            for (const auto& point : projected) {
                polygon.emplace_back(cvRound(point.x), cvRound(point.y));
            }
            const int shade = (x + y) % 2 == 0 ? 20 : 245;
            cv::fillConvexPoly(image, polygon, cv::Scalar(shade, shade, shade), cv::LINE_AA);
        }
    }
    return image;
}
}

void runCalibrationWorkflowTests()
{
    QTemporaryDir work;
    require(work.isValid(), "Calibration tests need an isolated temporary directory");
    StereoCameraConfig config;
    config.useMockProvider = true;
    config.frameCount = 10;
    config.outputDirectory = work.path().toStdString();
    CalibrationCaptureSessionService capture;
    const auto first = capture.captureCurrentFrame(config);
    require(capture.isActive() && first.capturedFrameCount == 1 &&
        first.leftImagePaths.size() == 1 && first.rightImagePaths.size() == 1 &&
        QFileInfo::exists(QString::fromStdString(first.leftImagePaths[0])) &&
        QFileInfo::exists(QString::fromStdString(first.rightImagePaths[0])),
        "The first capture action must automatically start and save its first stereo pair");
    const auto second = capture.captureCurrentFrame(config);
    require(second.sessionDirectory == first.sessionDirectory && second.capturedFrameCount == 2 &&
        second.leftImagePaths[0] == first.leftImagePaths[0] && second.leftImagePaths[1] != first.leftImagePaths[0],
        "Repeated capture actions must append paired frames without resetting the session");
    capture.finish();
    require(!capture.isActive() && capture.currentResult().capturedFrameCount == 2,
        "Ending capture automatically must release cameras while retaining the saved frames");
    const auto nextSession = capture.captureCurrentFrame(config);
    require(nextSession.sessionDirectory != second.sessionDirectory && nextSession.capturedFrameCount == 1 &&
        QFileInfo::exists(QString::fromStdString(second.leftImagePaths[1])),
        "A new capture session must not overwrite an earlier session");
    capture.finish();
    std::cout << "PASS: one-click first capture, paired appending and automatic session cleanup\n";

    AcquisitionPanel panel;
    auto* captureButton = panel.findChild<QPushButton*>("captureCalibrationFrame");
    auto* calibrateButton = panel.findChild<QPushButton*>("calibrateFrames");
    auto* exportButton = panel.findChild<QPushButton*>("exportCalibration");
    auto* fileEdit = panel.findChild<QLineEdit*>("calibrationFile");
    auto* tabs = panel.findChild<QTabWidget*>();
    require(captureButton && calibrateButton && exportButton && fileEdit && tabs && captureButton->isEnabled() &&
        !calibrateButton->isEnabled() && !exportButton->isEnabled(),
        "Capture must be available immediately; calibration needs images");
    for (auto* button : tabs->widget(0)->findChildren<QPushButton*>()) {
        require(button->text() == "..." || button == captureButton || button == calibrateButton || button == exportButton,
            "The calibration page must contain capture/calibrate/export actions and path selectors");
    }
    require(captureButton->text() == QString::fromUtf8("采集") && calibrateButton->text() == QString::fromUtf8("标定"),
        "Calibration actions must use the requested short titles");

    const QString left = QDir(work.path()).filePath("user_images/left");
    const QString right = QDir(work.path()).filePath("user_images/right");
    require(QDir().mkpath(left) && QDir().mkpath(right), "User image directories must be available");
    for (int pose = 0; pose < 6; ++pose) {
        const QString name = QString("pose_%1.png").arg(pose);
        require(cv::imwrite(QDir(left).filePath(name).toStdString(), chessboardImage(pose, false)) &&
            cv::imwrite(QDir(right).filePath(name).toStdString(), chessboardImage(pose, true)),
            "Synthetic stereo chessboard pairs must be saved");
    }
    auto project = panel.projectConfig();
    project.leftCalibrationDirectory = left.toStdString();
    project.rightCalibrationDirectory = right.toStdString();
    project.calibrationInput.boardSize = cv::Size(9, 6);
    project.calibrationInput.squareSize = cv::Size2d(25, 25);
    panel.setProjectConfig(project);
    require(calibrateButton->isEnabled(), "Choosing offline image directories must enable calibration without online capture");
    panel.setCalibrationCaptureState(true, 2);
    require(panel.calibrationInput().leftDirectory == left.toStdString() &&
        panel.calibrationInput().rightDirectory == right.toStdString(),
        "Explicit offline directories must remain the calibration inputs even after online capture");
    int autoFinish = 0;
    QObject::connect(&panel, &AcquisitionPanel::finishCalibrationCaptureRequested, [&]() { ++autoFinish; });
    panel.setBusy(true);
    require(!captureButton->isEnabled() && !calibrateButton->isEnabled() && !tabs->tabBar()->isEnabled(),
        "Busy state must prevent duplicate actions and switching away mid-capture");
    panel.setBusy(false);
    tabs->setCurrentIndex(1);
    require(autoFinish == 1, "Leaving the calibration page must request automatic camera release");
    panel.setCalibrationCaptureState(false, 2);
    tabs->setCurrentIndex(0);

    const QString savedFile = QDir(work.path()).filePath("calibration/result/auto_saved.yml");
    auto input = panel.calibrationInput();
    input.outputFile = savedFile.toStdString();
    CalibrationService service;
    const auto calibrated = service.calibrate(input);
    require(calibrated.isValid() && calibrated.successfulPairs == 6 && std::isfinite(calibrated.rms) && QFileInfo::exists(savedFile),
        "Offline calibration must automatically save a valid result without a separate save action");
    CalibrationResult loaded;
    require(service.loadCalibration(savedFile.toStdString(), loaded) && loaded.isValid() &&
        cv::norm(calibrated.K1 - loaded.K1) < 0.000001,
        "The automatically saved result must be usable when selected for loading");
    int selectedFiles = 0;
    QString selectedFile;
    QObject::connect(&panel, &AcquisitionPanel::calibrationFileSelected, [&](const QString& file) {
        ++selectedFiles;
        selectedFile = file;
    });
    panel.setCalibrationFile(savedFile.toStdString());
    QMetaObject::invokeMethod(fileEdit, "editingFinished", Qt::DirectConnection);
    require(selectedFiles == 0, "Automatic result-path updates must not trigger a second loading workflow");
    fileEdit->setText(savedFile);
    fileEdit->setModified(true);
    QMetaObject::invokeMethod(fileEdit, "editingFinished", Qt::DirectConnection);
    require(selectedFiles == 1 && selectedFile == savedFile,
        "Committing a user-entered calibration path must request loading immediately");
    QMetaObject::invokeMethod(fileEdit, "editingFinished", Qt::DirectConnection);
    require(selectedFiles == 1, "Leaving an unchanged file input must not repeat loading");
    const QString invalidFile = QDir(work.path()).filePath("invalid.yml");
    QFile invalid(invalidFile);
    require(invalid.open(QIODevice::WriteOnly), "Invalid file fixture must be available");
    invalid.write("this is not a calibration file");
    invalid.close();
    CalibrationResult rejected;
    require(!service.loadCalibration(invalidFile.toStdString(), rejected) && !rejected.isValid(),
        "Invalid selected calibration files must not become valid reconstruction inputs");
    std::cout << "PASS: simplified calibration UI, offline image calibration, automatic save and file selection loading\n";
}
