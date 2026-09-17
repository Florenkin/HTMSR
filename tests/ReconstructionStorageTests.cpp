#include "app/services/ReconstructionCaptureSessionService.h"
#include "app/services/ReconstructionStorage.h"
#include "app/ui/AcquisitionPanel.h"
#include "core/PointCloudService.h"
#include "core/ReconstructionService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <opencv2/imgcodecs.hpp>

#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace htmsr;
using namespace htmsr::app;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ReconstructionResult sampleResult()
{
    ReconstructionResult result;
    result.mergedPoints = {{1.25, 2.5, 3.75}, {-4, 5, 6}};
    return result;
}

void verifyCloud(const ReconstructionResult& result)
{
    PointCloudService service;
    for (const auto& path : {result.txtPath, result.pcdPath}) {
        const auto loaded = service.load(path);
        require(loaded.size() == result.mergedPoints.size(), "Saved TXT and PCD must contain every point");
        for (std::size_t index = 0; index < loaded.size(); ++index) {
            require((loaded[index] - result.mergedPoints[index]).norm() < 1e-5,
                "Saved point coordinates must survive reloading");
        }
    }
}

void verifyDirectoryWorkflow(const QString& workDirectory, const AcquisitionSessionResult& captured)
{
    AcquisitionPanel panel;
    auto* leftEdit = panel.findChild<QLineEdit*>("leftReconstructionDirectory");
    auto* rightEdit = panel.findChild<QLineEdit*>("rightReconstructionDirectory");
    auto* reconstructButton = panel.findChild<QPushButton*>("reconstructFrames");
    auto* captureButton = panel.findChild<QPushButton*>("startReconstructionCapture");
    require(captureButton && captureButton->text() == QString::fromUtf8("采集") &&
        reconstructButton && reconstructButton->text() == QString::fromUtf8("重建"), "Reconstruction actions must use the requested short titles");
    require(leftEdit && rightEdit && reconstructButton && leftEdit->text().isEmpty() && rightEdit->text().isEmpty() &&
        !reconstructButton->isEnabled(), "Reconstruction must initially have two empty directories and require both selections");

    auto oldProject = panel.projectConfig();
    oldProject.leftReconstructionDirectory = "old/capture/left";
    oldProject.rightReconstructionDirectory = "old/capture/right";
    oldProject.laserConfig.leftRoi = oldProject.laserConfig.rightRoi = cv::Rect(0, 0, 64, 48);
    panel.setProjectConfig(oldProject);
    require(leftEdit->text().isEmpty() && rightEdit->text().isEmpty() && panel.reconstructionInput({}).leftDirectory.empty() &&
        panel.reconstructionInput({}).rightDirectory.empty(), "Loading numeric settings must not restore stale reconstruction directories");

    panel.setReconstructionDirectories(captured.leftDirectory, captured.rightDirectory);
    require(leftEdit->text() == QString::fromStdString(captured.leftDirectory) &&
        rightEdit->text() == QString::fromStdString(captured.rightDirectory) && reconstructButton->isEnabled() &&
        panel.reconstructionInput({}).leftDirectory == captured.leftDirectory &&
        panel.reconstructionInput({}).rightDirectory == captured.rightDirectory,
        "The capture must fill both directory fields with the corresponding stereo inputs");

    const QString left = QDir(workDirectory).filePath("local_frames/camera_A_images");
    const QString right = QDir(workDirectory).filePath("another_location/camera_B_images");
    require(QDir().mkpath(left) && QDir().mkpath(right), "Offline reconstruction fixtures need independent stereo folders");
    for (int frame = 0; frame < 2; ++frame) {
        cv::Mat leftImage(48, 64, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::Mat rightImage = leftImage.clone();
        leftImage(cv::Rect(38 + frame, 0, 3, 48)).setTo(cv::Scalar(255, 255, 255));
        rightImage(cv::Rect(33 + frame, 0, 3, 48)).setTo(cv::Scalar(255, 255, 255));
        const QString name = QString("frame_%1.png").arg(frame + 1, 6, 10, QChar('0'));
        require(cv::imwrite(QDir(left).filePath(name).toStdString(), leftImage) &&
            cv::imwrite(QDir(right).filePath(name).toStdString(), rightImage),
            "Offline stereo images must be saved");
    }

    // 任意名称、不同父目录均可使用，当前选择优先于之前的在线采集。
    panel.setReconstructionDirectories({}, {});
    leftEdit->setText("  " + QDir::toNativeSeparators(left) + "  ");
    require(!reconstructButton->isEnabled() && panel.reconstructionInput({}).leftDirectory == left.toStdString() &&
        panel.reconstructionInput({}).rightDirectory.empty(), "Selecting only the left folder must not enable reconstruction");
    rightEdit->setText("  " + QDir::toNativeSeparators(right) + "  ");
    require(reconstructButton->isEnabled(), "Selecting both offline directories must enable reconstruction regardless of folder names");
    int requested = 0;
    QObject::connect(&panel, &AcquisitionPanel::reconstructCapturedFramesRequested, [&]() { ++requested; });
    reconstructButton->click();
    require(requested == 1, "The shared button must dispatch offline reconstruction without requiring online capture state");

    CalibrationResult calibration;
    calibration.K1 = (cv::Mat_<double>(3, 3) << 100, 0, 32, 0, 100, 24, 0, 0, 1);
    calibration.K2 = calibration.K1.clone();
    calibration.P1 = calibration.K1.clone();
    calibration.P2 = calibration.K2.clone();
    calibration.D1 = calibration.D2 = cv::Mat::zeros(1, 5, CV_64F);
    calibration.R = cv::Mat::eye(3, 3, CV_64F);
    calibration.t = (cv::Mat_<double>(3, 1) << -60, 0, 0);
    const auto input = panel.reconstructionInput(calibration);
    require(input.leftDirectory == left.toStdString() && input.rightDirectory == right.toStdString(),
        "Offline inputs must use the currently selected directory, not the earlier capture");
    auto reconstructed = ReconstructionService{}.reconstruct(input);
    require(reconstructed.success && reconstructed.frames.size() == 2 && !reconstructed.mergedPoints.empty(),
        "Selected offline frames must reconstruct actual points with the production core service");
    for (const auto& frame : reconstructed.frames) {
        require(QFileInfo(QString::fromStdString(frame.leftImagePath)).absolutePath() == left &&
            QFileInfo(QString::fromStdString(frame.rightImagePath)).absolutePath() == right,
            "Every reconstructed frame must come from the user-selected directory");
    }
    ReconstructionStorage::savePointClouds(workDirectory.toStdString(), reconstructed);
    verifyCloud(reconstructed);

    panel.setBusy(true);
    require(!reconstructButton->isEnabled() && !leftEdit->isEnabled() && !rightEdit->isEnabled() &&
        !leftEdit->parentWidget()->findChild<QPushButton*>()->isEnabled() &&
        !rightEdit->parentWidget()->findChild<QPushButton*>()->isEnabled(),
        "A running task must lock both sources and prevent duplicate reconstruction");
    panel.setBusy(false);
    leftEdit->clear();
    require(!reconstructButton->isEnabled() && panel.reconstructionInput({}).leftDirectory.empty() &&
        panel.reconstructionInput({}).rightDirectory == right.toStdString(), "Clearing the left folder must preserve the right folder and disable reconstruction");
    leftEdit->setText(left);
    rightEdit->clear();
    require(!reconstructButton->isEnabled() && panel.reconstructionInput({}).leftDirectory == left.toStdString() &&
        panel.reconstructionInput({}).rightDirectory.empty(), "Clearing the right folder must preserve the left folder and disable reconstruction");
    std::cout << "PASS: two empty initial directories, capture autofill, arbitrary offline folder names and automatic reconstructed-cloud saving\n";
}
}

void runReconstructionStorageTests()
{
    QTemporaryDir work;
    require(work.isValid(), "Reconstruction saving tests need an isolated directory");
    const auto root = work.path().toStdString();
    const auto fixedTime = QDateTime::fromString("20260917_173001_123", "yyyyMMdd_HHmmss_zzz");
    const QString captureRoot = QDir(work.path()).filePath("reconstruction/capture");
    const QString resultRoot = QDir(work.path()).filePath("reconstruction/result");

    const QString firstSession = ReconstructionStorage::createCaptureDirectory(root, fixedTime);
    const QString secondSession = ReconstructionStorage::createCaptureDirectory(root, fixedTime);
    require(firstSession == QDir(captureRoot).filePath("20260917_173001_123") &&
        secondSession == firstSession + "_1", "Capture folders must use timestamps without reusing an existing session");

    auto first = sampleResult();
    ReconstructionStorage::savePointClouds(root, first, firstSession.toStdString());
    require(QString::fromStdString(first.txtPath) == QDir(resultRoot).filePath("point_cloud_20260917_173001_123.txt") &&
        QString::fromStdString(first.pcdPath) == QDir(resultRoot).filePath("point_cloud_20260917_173001_123.pcd"),
        "The corresponding cloud files must use the capture timestamp inside reconstruction/result");
    verifyCloud(first);

    auto repeated = sampleResult();
    repeated.mergedPoints[0].x() = 99;
    ReconstructionStorage::savePointClouds(root, repeated, firstSession.toStdString());
    require(repeated.txtPath != first.txtPath && QFileInfo(QString::fromStdString(repeated.txtPath)).completeBaseName() ==
        "point_cloud_20260917_173001_123_1", "Rebuilding a captured session must preserve earlier cloud files");
    verifyCloud(first);
    verifyCloud(repeated);

    auto offline = sampleResult();
    ReconstructionStorage::savePointClouds(root, offline, {}, fixedTime.addSecs(1));
    require(QFileInfo(QString::fromStdString(offline.txtPath)).completeBaseName() == "point_cloud_20260917_173002_123",
        "Offline reconstruction must use its own timestamp");
    verifyCloud(offline);
    auto legacy = sampleResult();
    ReconstructionStorage::savePointClouds(root, legacy, "old/reconstruction_capture_20260916_120000_000_abcd1234");
    require(QFileInfo(QString::fromStdString(legacy.txtPath)).completeBaseName() ==
        "point_cloud_20260916_120000_000_abcd1234", "Existing capture folder names must remain usable");

    // 已有 PCD 单文件也不能被覆盖；TXT/PCD 必须共用相同文件名。
    const QString occupiedPcd = QDir(resultRoot).filePath("point_cloud_20260917_173003_123.pcd");
    QFile occupied(occupiedPcd);
    require(occupied.open(QIODevice::WriteOnly) && occupied.write("existing cloud") == 14,
        "A historical PCD fixture must be writable");
    occupied.close();
    auto collision = sampleResult();
    ReconstructionStorage::savePointClouds(root, collision, {}, fixedTime.addSecs(2));
    require(QFileInfo(QString::fromStdString(collision.txtPath)).completeBaseName() == "point_cloud_20260917_173003_123_1",
        "A collision with either output format must reserve a new pair of paths");
    require(occupied.open(QIODevice::ReadOnly) && occupied.readAll() == "existing cloud", "Historical PCD contents must remain intact");
    occupied.close();

    QTemporaryDir noOutput;
    auto failed = sampleResult();
    failed.success = false;
    ReconstructionStorage::savePointClouds(noOutput.path().toStdString(), failed);
    ReconstructionResult empty;
    ReconstructionStorage::savePointClouds(noOutput.path().toStdString(), empty);
    require(QDir(noOutput.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(),
        "Failed or empty reconstruction must not leave point cloud files");

    const QString blockedRoot = QDir(work.path()).filePath("blocked");
    QFile blocked(blockedRoot);
    require(blocked.open(QIODevice::WriteOnly), "A blocked output fixture must be created");
    blocked.close();
    bool rejected = false;
    try {
        auto result = sampleResult();
        ReconstructionStorage::savePointClouds(blockedRoot.toStdString(), result);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "An unwritable output location must report saving failure");

    std::vector<ReconstructionResult> concurrent(4, sampleResult());
    std::vector<std::exception_ptr> errors(4);
    std::vector<std::thread> workers;
    for (std::size_t index = 0; index < concurrent.size(); ++index) {
        workers.emplace_back([&, index] {
            try {
                concurrent[index].mergedPoints[0].x() = static_cast<double>(index);
                ReconstructionStorage::savePointClouds(root, concurrent[index], {}, fixedTime.addSecs(3));
            } catch (...) {
                errors[index] = std::current_exception();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (std::size_t index = 0; index < concurrent.size(); ++index) {
        if (errors[index]) {
            std::rethrow_exception(errors[index]);
        }
        verifyCloud(concurrent[index]);
        for (std::size_t other = 0; other < index; ++other) {
            require(concurrent[index].txtPath != concurrent[other].txtPath, "Concurrent saving must never overwrite another result");
        }
    }

    IntegratedScanConfig config;
    config.stereoCamera.useMockProvider = true;
    config.stereoCamera.frameCount = 2;
    config.stereoCamera.outputDirectory = root;
    config.totalRotationAngleDeg = config.galvo.stepAngleDeg * config.stereoCamera.frameCount;
    ReconstructionCaptureSessionService capture;
    const auto captured = capture.capture(config);
    require(captured.success && captured.capturedFrameCount == 2 && captured.leftImagePaths.size() == 2 &&
        captured.rightImagePaths.size() == 2, "The capture service must save all paired reconstruction frames");
    const QString capturePath = QString::fromStdString(captured.sessionDirectory);
    require(QFileInfo(capturePath).absolutePath() == QFileInfo(captureRoot).absoluteFilePath() &&
        QRegularExpression("^\\d{8}_\\d{6}_\\d{3}(?:_\\d+)?$").match(QFileInfo(capturePath).fileName()).hasMatch(),
        "Actual capture sessions must be timestamp folders below reconstruction/capture");
    require(!cv::imread(captured.leftImagePaths.front()).empty() && !cv::imread(captured.rightImagePaths.back()).empty(),
        "Saved capture files must be readable images");
    auto capturedResult = sampleResult();
    ReconstructionStorage::savePointClouds(root, capturedResult, captured.sessionDirectory);
    require(QFileInfo(QString::fromStdString(capturedResult.txtPath)).completeBaseName() ==
        "point_cloud_" + QFileInfo(capturePath).fileName(), "Actual captured frames and cloud files must share their timestamp");
    verifyCloud(capturedResult);
    verifyDirectoryWorkflow(work.path(), captured);
    std::cout << "PASS: reconstruction capture/result directories, timestamp correspondence, serialization and collision preservation\n";
}
