#include "core/Cancellation.h"
#include "core/CalibrationService.h"
#include "core/ReconstructionService.h"
#include "app/services/ReconstructionCaptureSessionService.h"
#include <QTemporaryDir>
#include <QDirIterator>
#include <future>
#include <iostream>

using namespace htmsr;
void runCancellationTests()
{
    const auto require = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
    CancellationToken cancelled;
    cancelled.request();
    const auto expectCancelled = [&](auto operation) {
        bool caught = false;
        try { operation(); } catch (const OperationCancelled&) { caught = true; }
        require(caught, "Cancellation must stop work, not return success");
    };
    expectCancelled([&] { CalibrationService{}.calibrate({}, cancelled); });
    expectCancelled([&] { ReconstructionService{}.reconstruct({}, cancelled); });
    CancellationToken waiting;
    auto wait = std::async(std::launch::async, [waiting] {
        try { waiting.wait(60000); } catch (const OperationCancelled&) { return true; }
        return false;
    });
    waiting.request();
    require(wait.wait_for(std::chrono::seconds(2)) == std::future_status::ready && wait.get(),
        "Cancellation must interrupt a long application wait");
    QTemporaryDir root;
    require(root.isValid(), "Temporary output must exist");
    app::IntegratedScanConfig config;
    config.stereoCamera.useMockProvider = true;
    config.stereoCamera.frameCount = 10;
    config.stereoCamera.outputDirectory = root.path().toStdString();
    CancellationToken capture;
    int callbacks = 0;
    expectCancelled([&] {
        app::ReconstructionCaptureSessionService{}.capture(config,
            [&](int, int, const app::FramePair&) { ++callbacks; capture.request(); }, capture);
    });
    require(callbacks == 1, "Cancelled capture must not continue taking frames");
    int images = 0;
    QDirIterator entries(root.path(), {"*.bmp"}, QDir::Files, QDirIterator::Subdirectories);
    while (entries.hasNext()) { entries.next(); ++images; }
    require(images == 2, "Cancellation must preserve the completed stereo pair only");
    std::cout << "PASS: cancellation before algorithms, interrupted waits and partial mock capture\n";
}
