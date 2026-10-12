#include "app/services/ReconstructionCaptureSessionService.h"
#include "app/services/LaserExtractionStorage.h"
#include "app/services/ReconstructionStorage.h"
#include "app/ui/AcquisitionPanel.h"
#include "app/ui/CaptureReviewWidget.h"
#include "app/ui/MainWindowViewModel.h"
#include "core/PointCloudService.h"
#include "core/ReconstructionService.h"

#include <QDir>
#include <QCheckBox>
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

cv::Mat readImage(const std::string& path)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    const cv::Mat encoded(1, bytes.size(), CV_8UC1,
        const_cast<char*>(bytes.constData()));
    return cv::imdecode(encoded, cv::IMREAD_COLOR);
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

void verifyPcdCompatibility(const QString& workDirectory)
{
    const QString asciiPath = QDir(workDirectory).filePath("external_ascii.pcd");
    QFile ascii(asciiPath);
    const QByteArray asciiContents =
        "# externally generated PCD with reordered fields\n"
        "VERSION 0.7\n"
        "FIELDS intensity z x y\n"
        "SIZE 4 8 8 8\n"
        "TYPE U F F F\n"
        "COUNT 1 1 1 1\n"
        "WIDTH 2\nHEIGHT 1\nPOINTS 2\nDATA ascii\n"
        "7 3.75 1.25 2.5\n"
        "9 6 -4 5\n";
    require(ascii.open(QIODevice::WriteOnly) && ascii.write(asciiContents) == asciiContents.size(),
        "ASCII PCD fixture must be writable");
    ascii.close();

    const auto loaded = PointCloudService{}.load(asciiPath.toStdString());
    require(loaded.size() == 2 && (loaded[0] - Eigen::Vector3d(1.25, 2.5, 3.75)).norm() < 1e-9 &&
        (loaded[1] - Eigen::Vector3d(-4, 5, 6)).norm() < 1e-9,
        "Native PCD reader must support ASCII files, extra fields and reordered XYZ columns");

    const QString unsupportedPath = QDir(workDirectory).filePath("compressed.pcd");
    QFile unsupported(unsupportedPath);
    const QByteArray unsupportedContents =
        "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\n"
        "WIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA binary_compressed\n";
    require(unsupported.open(QIODevice::WriteOnly) &&
        unsupported.write(unsupportedContents) == unsupportedContents.size(), "Unsupported PCD fixture must be writable");
    unsupported.close();
    bool rejected = false;
    try {
        (void)PointCloudService{}.load(unsupportedPath.toStdString());
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Unsupported compressed PCD must report a clear loading failure");
}

void verifyLaserExtractionStorage(const QString& workDirectory)
{
    const QStringList tabTitles = mainWindowCentralTabTitles();
    require(tabTitles.size() == 5 && tabTitles.at(0) == QString::fromUtf8("双目") &&
            tabTitles.at(1) == QString::fromUtf8("点云") && tabTitles.at(2) == QString::fromUtf8("采集") &&
            tabTitles.at(3) == QString::fromUtf8("激光线") && tabTitles.at(4) == QString::fromUtf8("命令"),
        "Laser extraction tab must appear between capture and commands");

    const QString root = QDir(workDirectory).filePath(QString::fromUtf8("激光线输出"));
    const auto fixedTime = QDateTime::fromString("20260925_203000_123", "yyyyMMdd_HHmmss_zzz");
    cv::Mat left(24, 32, CV_8UC3, cv::Scalar(20, 30, 40));
    cv::Mat right(24, 32, CV_8UC3, cv::Scalar(50, 60, 70));
    left.at<cv::Vec3b>(12, 8) = cv::Vec3b(0, 0, 255);
    right.at<cv::Vec3b>(12, 9) = cv::Vec3b(0, 0, 255);

    LaserExtractionStorage first(root.toStdString(), fixedTime);
    require(first.isReady() && first.savePair(0, left, right),
        "Laser extraction storage must create a Unicode session and save a complete pair");
    const auto firstResult = first.result();
    require(QFileInfo(QString::fromStdString(firstResult.sessionDirectory)).fileName() == "20260925_203000_123" &&
        firstResult.leftImagePaths.size() == 1 && firstResult.rightImagePaths.size() == 1,
        "Laser extraction results must expose one timestamped left/right pair");
    const cv::Mat savedLeft = readImage(firstResult.leftImagePaths.front());
    require(!savedLeft.empty() && savedLeft.size() == left.size() &&
        savedLeft.at<cv::Vec3b>(12, 8) == cv::Vec3b(0, 0, 255),
        "Lossless PNG saving must preserve the red centerline overlay");

    LaserExtractionStorage second(root.toStdString(), fixedTime);
    require(second.isReady() && second.result().sessionDirectory != firstResult.sessionDirectory,
        "Consecutive laser extraction sessions must not overwrite historical images");
    require(!second.savePair(1, left, {}) && second.result().failedPairCount == 1 &&
        second.result().leftImagePaths.empty() && second.result().rightImagePaths.empty() &&
        !QFileInfo(QDir(QString::fromStdString(second.result().sessionDirectory))
            .filePath("left/frame_000002.png")).exists(),
        "An incomplete image pair must be removed and reported without exposing a partial result");

    const QString blockedPath = QDir(workDirectory).filePath("blocked_laser_output");
    QFile blocked(blockedPath);
    require(blocked.open(QIODevice::WriteOnly), "Blocked laser output fixture must be writable");
    blocked.close();
    LaserExtractionStorage unavailable(blockedPath.toStdString(), fixedTime);
    require(!unavailable.isReady() && unavailable.result().failedPairCount > 0,
        "An unavailable laser output directory must be reported as a non-fatal storage failure");

    CaptureReviewWidget viewer(CaptureReviewWidget::Mode::LaserExtractionReadOnly);
    viewer.setImagePairs(firstResult.sessionDirectory, firstResult.leftImagePaths, firstResult.rightImagePaths);
    require(viewer.captureResult().capturedFrameCount == 1,
        "Read-only laser extraction viewer must load complete pairs after reconstruction");
    for (auto* button : viewer.findChildren<QPushButton*>()) {
        require(button->text() != QString::fromUtf8("删除"),
            "Read-only laser extraction viewer must not expose deletion actions");
    }
}

void verifyDirectoryWorkflow(const QString& workDirectory, const AcquisitionSessionResult& captured)
{
    AcquisitionPanel panel;
    auto* leftEdit = panel.findChild<QLineEdit*>("leftReconstructionDirectory");
    auto* rightEdit = panel.findChild<QLineEdit*>("rightReconstructionDirectory");
    auto* laserEdit = panel.findChild<QLineEdit*>("laserExtractionDirectory");
    auto* saveLaserImages = panel.findChild<QCheckBox*>("saveLaserExtractionImages");
    auto* reconstructButton = panel.findChild<QPushButton*>("reconstructFrames");
    auto* captureButton = panel.findChild<QPushButton*>("startReconstructionCapture");
    require(captureButton && captureButton->text() == QString::fromUtf8("采集") &&
        reconstructButton && reconstructButton->text() == QString::fromUtf8("重建"), "Reconstruction actions must use the requested short titles");
    require(leftEdit && rightEdit && laserEdit && saveLaserImages && reconstructButton &&
        leftEdit->text().isEmpty() && rightEdit->text().isEmpty() &&
        !reconstructButton->isEnabled(), "Reconstruction must initially have two empty directories and require both selections");
    require(laserEdit->text() == "output/LaserExtraction" && !saveLaserImages->isChecked(),
        "Laser extraction images must default to output/LaserExtraction and remain disabled");

    auto oldProject = panel.projectConfig();
    oldProject.restoreInputPaths = false;
    oldProject.leftReconstructionDirectory = "old/capture/left";
    oldProject.rightReconstructionDirectory = "old/capture/right";
    oldProject.laserConfig.leftRoi = oldProject.laserConfig.rightRoi = cv::Rect(0, 0, 64, 48);
    panel.setProjectConfig(oldProject);
    require(leftEdit->text().isEmpty() && rightEdit->text().isEmpty() && panel.reconstructionInput({}).leftDirectory.empty() &&
        panel.reconstructionInput({}).rightDirectory.empty(), "Disabling path restoration must leave reconstruction directories empty");
    require(laserEdit->text() == "output/LaserExtraction",
        "Disabling input path restoration must not clear the laser extraction output directory");

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
    const QString laserRoot = QDir(workDirectory).filePath(QString::fromUtf8("回调激光线"));
    LaserExtractionStorage laserStorage(laserRoot.toStdString(),
        QDateTime::fromString("20260925_210000_001", "yyyyMMdd_HHmmss_zzz"));
    auto reconstructed = ReconstructionService{}.reconstruct(input, {},
        [&laserStorage](int frameIndex, const cv::Mat& leftPreview, const cv::Mat& rightPreview) {
            laserStorage.savePair(frameIndex, leftPreview, rightPreview);
        });
    require(reconstructed.success && reconstructed.frames.size() == 2 && !reconstructed.mergedPoints.empty(),
        "Selected offline frames must reconstruct actual points with the production core service");
    require(laserStorage.result().leftImagePaths.size() == 2 &&
        laserStorage.result().rightImagePaths.size() == 2,
        "The reconstruction preview callback must save every processable frame as a complete pair");
    const cv::Mat overlay = readImage(laserStorage.result().leftImagePaths.front());
    cv::Mat redMask;
    cv::inRange(overlay, cv::Scalar(0, 0, 250), cv::Scalar(5, 5, 255), redMask);
    require(cv::countNonZero(redMask) > 0,
        "Saved reconstruction previews must contain red centerline markers over the source image");
    for (const auto& frame : reconstructed.frames) {
        require(QFileInfo(QString::fromStdString(frame.leftImagePath)).absolutePath() == left &&
            QFileInfo(QString::fromStdString(frame.rightImagePath)).absolutePath() == right,
            "Every reconstructed frame must come from the user-selected directory");
    }
    ReconstructionStorage::savePointClouds(workDirectory.toStdString(), reconstructed);
    verifyCloud(reconstructed);

    auto emptyLineInput = input;
    emptyLineInput.laserConfig.grayThreshold = 255;
    LaserExtractionStorage emptyLineStorage(laserRoot.toStdString(),
        QDateTime::fromString("20260925_210001_001", "yyyyMMdd_HHmmss_zzz"));
    const auto emptyLineResult = ReconstructionService{}.reconstruct(emptyLineInput, {},
        [&emptyLineStorage](int frameIndex, const cv::Mat& leftPreview, const cv::Mat& rightPreview) {
            emptyLineStorage.savePair(frameIndex, leftPreview, rightPreview);
        });
    require(!emptyLineResult.success && emptyLineStorage.result().leftImagePaths.size() == 2 &&
        emptyLineStorage.result().rightImagePaths.size() == 2,
        "Frames with empty extracted lines must still retain diagnostic preview pairs");

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
    verifyPcdCompatibility(work.path());
    verifyLaserExtractionStorage(work.path());
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
