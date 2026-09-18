#include "app/acquisition/HardwareTriggeredStereoCapture.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <future>
#include <stdexcept>
#include <thread>

using namespace htmsr::app;

void runGalvoCaptureWorkflowTests();

namespace {

void check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<CameraFrame> frames(int count, std::uint32_t firstNumber = 1, std::uint32_t firstTrigger = 0)
{
    std::vector<CameraFrame> result;
    for (int i = 0; i < count; ++i) {
        CameraFrame frame;
        frame.image = cv::Mat(8, 8, CV_8UC1, cv::Scalar(i % 256)).clone();
        frame.frameNumber = firstNumber + i;
        frame.triggerIndex = firstTrigger == 0 ? 0 : firstTrigger + i;
        frame.metadataValid = true;
        result.push_back(std::move(frame));
    }
    return result;
}

HardwareTriggeredStereoCapture::FrameReader reader(
    std::vector<CameraFrame> sequence, std::atomic<bool>& started, int initialTimeouts = 0)
{
    return [sequence = std::move(sequence), &started, position = std::size_t{0}, initialTimeouts](int timeoutMs) mutable {
        if (!started.load() || initialTimeouts > 0 || position == sequence.size()) {
            if (started.load() && initialTimeouts > 0) {
                --initialTimeouts;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeoutMs, 2)));
            return CameraFrame{};
        }
        return sequence[position++];
    };
}

HardwareCaptureReport run(std::vector<CameraFrame> left, std::vector<CameraFrame> right, int expected,
    int leftTimeouts = 0, int rightTimeouts = 0, int writerDelayMs = 0,
    std::size_t queueBytes = 1024 * 1024, bool writerFails = false, bool startFails = false)
{
    std::atomic<bool> started{false};
    HardwareCaptureOptions options;
    options.expectedFrameCount = expected;
    options.frameTimeoutMs = 500; // Windows 的短睡眠可能按系统时钟粒度上取整，不能把几次暂时无数据误判为永久缺帧。
    options.pollTimeoutMs = 2;
    options.extraFrameCheckMs = 2;
    options.maxQueuedBytesPerCamera = queueBytes;
    HardwareTriggeredStereoCapture capture;
    int saved = 0;
    auto report = capture.capture(options, reader(std::move(left), started, leftTimeouts),
        reader(std::move(right), started, rightTimeouts),
        [&]() {
            if (startFails) {
                throw std::runtime_error("simulated scan start failure");
            }
            started.store(true);
        },
        [&](const HardwareStereoFrame& pair) {
            if (writerFails) {
                throw std::runtime_error("simulated disk write failure");
            }
            check(pair.frameIndex == saved, "Save index was skipped");
            check(pair.left.image.at<unsigned char>(0, 0) == pair.right.image.at<unsigned char>(0, 0), "Stereo images were mismatched");
            if (writerDelayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(writerDelayMs));
            }
            ++saved;
        });
    check(report.savedPairs == saved, "Saved report count differs from writer calls");
    return report;
}

void expectFailure(const HardwareCaptureReport& report, const char* cause)
{
    check(!report.success, "Incomplete capture was marked successful");
    if (report.message.find(cause) == std::string::npos) {
        throw std::runtime_error("Expected failure cause '" + std::string(cause) + "', received: " + report.message);
    }
}

void delayedScanStartDoesNotBlockSaving(bool failAfterFrames)
{
    constexpr int count = 40;
    std::atomic<bool> started{false};
    std::promise<void> framesSaved;
    auto finished = framesSaved.get_future();
    const auto pacedReader = [&]() {
        return [read = reader(frames(count), started)](int timeoutMs) mutable {
            auto frame = read(timeoutMs);
            if (!frame.image.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return frame;
        };
    };
    HardwareCaptureOptions options;
    options.expectedFrameCount = count;
    options.frameTimeoutMs = 3000;
    options.pollTimeoutMs = 2;
    options.extraFrameCheckMs = 2;
    options.maxQueuedBytesPerCamera = 8 * 64; // Eight frames; never enough to hold the scan until startup returns.
    int saved = 0;
    HardwareTriggeredStereoCapture capture;
    const auto report = capture.capture(options, pacedReader(), pacedReader(), [&]() {
        started.store(true); // Hardware can emit pulses before the serial start call returns.
        check(finished.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
            "Saving waited for scan-start callback to return");
        if (failAfterFrames) throw std::runtime_error("scan start failure after emitting frames");
    }, [&](const HardwareStereoFrame& pair) {
        check(pair.frameIndex == saved, "Delayed startup changed save ordering");
        check(pair.left.frameNumber == pair.right.frameNumber, "Delayed startup mismatched cameras");
        if (++saved == count) framesSaved.set_value();
    });
    check(report.savedPairs == count && saved == count, "Frames were lost during delayed startup");
    check(report.leftPeakQueuedBytes <= options.maxQueuedBytesPerCamera &&
        report.rightPeakQueuedBytes <= options.maxQueuedBytesPerCamera, "Queue exceeded its memory limit");
    if (failAfterFrames) {
        expectFailure(report, "scan start failure");
    } else {
        check(report.success, "Delayed startup overflowed the save queue");
        check(report.scanStartMs >= 100.0, "Delayed-start timing was not recorded");
    }
}

} // namespace

int main()
{
    try {
        runGalvoCaptureWorkflowTests();
        IntegratedScanConfig defaults;
        check(defaults.galvo.syncMode == GalvoSyncMode::Sync, "Galvo must default to Sync");
        check(defaults.stereoCamera.leftParameters.useHardwareTrigger && defaults.stereoCamera.rightParameters.useHardwareTrigger,
            "Reconstruction must default to hardware trigger");
        check(defaults.stereoCamera.leftParameters.triggerSourceLine == 5 && defaults.stereoCamera.rightParameters.triggerSourceLine == 5,
            "Both cameras must default to Line5");
        check(!CameraParameterConfig{}.useHardwareTrigger, "Ordinary camera/preview must remain free-run by default");

        auto complete = run(frames(20, 100, 10), frames(20, 900, 300), 20);
        check(complete.success && complete.leftReceived == 20 && complete.rightReceived == 20 && complete.savedPairs == 20,
            "Different device frame counter bases should still form complete pairs");
        std::cout << "PASS: defaults and complete stereo capture with independent counters\n";
        complete = run(frames(2000), frames(2000), 2000);
        check(complete.success && complete.savedPairs == 2000, "Default scan count of 2000 pairs was not preserved");
        std::cout << "PASS: 2000 rotation steps preserve exactly 2000 left/right pairs\n";

        delayedScanStartDoesNotBlockSaving(false);
        delayedScanStartDoesNotBlockSaving(true);
        std::cout << "PASS: saving runs while the scan-start call is blocked; a late startup failure still rejects capture\n";

        complete = run(frames(100), frames(100), 100, 3, 5, 1);
        if (!complete.success) {
            std::cerr << "Transient capture report: " << complete.message << '\n';
        }
        check(complete.success && complete.savedPairs == 100, "Transient timeouts/slow writing caused lost pairs");
        std::cout << "PASS: transient single-camera timeouts retry the same slot; slower saving does not block reception\n";

        auto gap = frames(5);
        gap[2].frameNumber += 1;
        expectFailure(run(gap, frames(5), 5), "sequence gap/duplicate");
        auto duplicate = frames(5);
        duplicate[1].frameNumber = duplicate[0].frameNumber;
        expectFailure(run(frames(5), duplicate, 5), "sequence gap/duplicate");
        std::cout << "PASS: frame gaps and duplicate frame IDs are rejected\n";

        expectFailure(run(frames(5), frames(4), 5), "timed out");
        expectFailure(run(frames(5), {}, 5), "timed out");
        std::cout << "PASS: missing last trigger and one-sided trigger failure cannot report success\n";

        auto triggerGap = frames(5, 1, 100);
        triggerGap[1].triggerIndex += 1;
        expectFailure(run(triggerGap, frames(5), 5), "trigger count gap/duplicate");
        auto lostPacket = frames(5);
        lostPacket[1].lostPacketCount = 1;
        expectFailure(run(lostPacket, frames(5), 5), "incomplete frame");
        auto invalidMetadata = frames(5);
        invalidMetadata[0].metadataValid = false;
        expectFailure(run(frames(5), invalidMetadata, 5), "invalid metadata");
        std::cout << "PASS: trigger gaps, transport loss and invalid metadata are rejected\n";

        expectFailure(run(frames(100), frames(100), 100, 0, 0, 5, 64), "queue is full");
        expectFailure(run(frames(5), frames(5), 5, 0, 0, 0, 1024 * 1024, true), "disk write failure");
        expectFailure(run(frames(5), frames(5), 5, 0, 0, 0, 1024 * 1024, false, true), "scan start failure");
        std::cout << "PASS: queue overflow, disk failure and start failure exit safely\n";

        expectFailure(run(frames(6), frames(5), 5), "more frames");
        complete = run(frames(3, 0xFFFFFFFEu), frames(3, 20), 3);
        check(complete.success, "32-bit frame counter wrap was incorrectly rejected");
        std::cout << "PASS: extra trigger is rejected and 32-bit counter wrap is accepted\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << '\n';
        return 1;
    }
}
