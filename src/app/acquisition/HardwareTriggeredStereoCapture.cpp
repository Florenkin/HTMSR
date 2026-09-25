#include "app/acquisition/HardwareTriggeredStereoCapture.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace htmsr::app {

HardwareCaptureReport HardwareTriggeredStereoCapture::capture(
    const HardwareCaptureOptions& options,
    FrameReader leftReader,
    FrameReader rightReader,
    std::function<void()> startScan,
    PairWriter savePair) const
{
    if (options.expectedFrameCount <= 0 || options.frameTimeoutMs <= 0 ||
        options.pollTimeoutMs <= 0 || options.extraFrameCheckMs < 0 ||
        options.maxQueuedBytesPerCamera == 0 || !leftReader || !rightReader || !startScan || !savePair) {
        throw std::invalid_argument("Invalid hardware stereo capture configuration.");
    }

    struct CameraQueue {
        std::deque<CameraFrame> frames;
        std::size_t bytes = 0;
        std::size_t peakBytes = 0;
        std::chrono::steady_clock::time_point firstReceivedAt;
        int received = 0;
        bool done = false;
    };
    CameraQueue leftQueue, rightQueue;
    HardwareCaptureReport report;
    std::mutex mutex;
    std::condition_variable changed;
    bool stopped = false;
    int readyReaders = 0;
    bool writerReady = false;
    std::string error;

    const auto fail = [&](const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (error.empty()) {
            error = message;
        }
        stopped = true;
        changed.notify_all();
    };
    const auto isStopped = [&]() {
        if (options.cancellation.requested()) fail("操作已取消，采集未完整完成。");
        std::lock_guard<std::mutex> lock(mutex);
        return stopped;
    };
    const auto byteSize = [](const CameraFrame& frame) {
        return frame.image.total() * frame.image.elemSize();
    };

    const auto receive = [&](FrameReader reader, CameraQueue& queue, const char* side) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++readyReaders;
            changed.notify_all();
        }
        try {
            CameraFrame previous;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.frameTimeoutMs);
            for (int index = 0; index < options.expectedFrameCount && !isStopped();) {
                // 超时重试同一位置，不推进序号，也不丢弃另一台相机已经接收到的帧。
                CameraFrame frame = reader(std::min(options.pollTimeoutMs, options.frameTimeoutMs));
                if (frame.image.empty()) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        throw std::runtime_error(std::string(side) + " camera timed out waiting for trigger frame " +
                            std::to_string(index + 1) + "/" + std::to_string(options.expectedFrameCount) +
                            ". Check Line5 wiring, shared trigger pulses, and camera throughput.");
                    }
                    continue;
                }
                if (!frame.metadataValid || frame.lostPacketCount != 0) {
                    throw std::runtime_error(std::string(side) + " camera returned invalid metadata or an incomplete frame, lostPackets=" +
                        std::to_string(frame.lostPacketCount));
                }
                if (index > 0 && static_cast<std::uint32_t>(frame.frameNumber - previous.frameNumber) != 1u) {
                    throw std::runtime_error(std::string(side) + " camera frame sequence gap/duplicate: previous=" +
                        std::to_string(previous.frameNumber) + ", current=" + std::to_string(frame.frameNumber) +
                        ". Capture is incomplete; later images will not be shifted into the missing position.");
                }
                // 部分机型未提供触发计数（始终为 0）；一旦提供则必须每帧递增一次。
                if (index > 0 && (frame.triggerIndex != 0 || previous.triggerIndex != 0) &&
                    static_cast<std::uint32_t>(frame.triggerIndex - previous.triggerIndex) != 1u) {
                    throw std::runtime_error(std::string(side) + " camera trigger count gap/duplicate: previous=" +
                        std::to_string(previous.triggerIndex) + ", current=" + std::to_string(frame.triggerIndex));
                }
                previous = frame;
                deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.frameTimeoutMs);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (stopped) {
                        break;
                    }
                    ++queue.received;
                    if (queue.received == 1) {
                        queue.firstReceivedAt = std::chrono::steady_clock::now();
                    }
                    const std::size_t bytes = byteSize(frame);
                    if (bytes > options.maxQueuedBytesPerCamera - queue.bytes) {
                        throw std::runtime_error(std::string(side) + " camera save queue is full: queuedBytes=" +
                            std::to_string(queue.bytes) + ", incomingBytes=" + std::to_string(bytes) +
                            ", limitBytes=" + std::to_string(options.maxQueuedBytesPerCamera) +
                            ". Saving is not keeping up with incoming triggers. Check startup/save timings and trigger interval. "
                            "Capture is incomplete; later frames will not be shifted into missing positions.");
                    }
                    queue.bytes += bytes;
                    queue.peakBytes = std::max(queue.peakBytes, queue.bytes);
                    queue.frames.push_back(std::move(frame));
                    ++index;
                    changed.notify_all();
                }
            }
            if (!isStopped() && options.extraFrameCheckMs > 0) {
                const CameraFrame extra = reader(options.extraFrameCheckMs);
                if (!extra.image.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        ++queue.received;
                    }
                    throw std::runtime_error(std::string(side) + " camera received more frames than the configured rotation steps. "
                        "Check duplicate pulses or repeated scans.");
                }
            }
        } catch (const std::exception& ex) {
            fail(ex.what());
        } catch (...) {
            fail(std::string(side) + " camera receive failed with an unknown exception.");
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.done = true;
            changed.notify_all();
        }
    };

    const auto writePairs = [&]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            writerReady = true;
            changed.notify_all();
        }
        try {
            for (int index = 0; index < options.expectedFrameCount; ++index) {
                HardwareStereoFrame pair;
                pair.frameIndex = index;
                std::chrono::steady_clock::time_point firstPairReceivedAt;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    changed.wait(lock, [&]() {
                        return stopped || (!leftQueue.frames.empty() && !rightQueue.frames.empty()) ||
                            (leftQueue.done && leftQueue.frames.empty()) || (rightQueue.done && rightQueue.frames.empty());
                    });
                    if (stopped) {
                        break;
                    }
                    if (leftQueue.frames.empty() || rightQueue.frames.empty()) {
                        throw std::runtime_error("Stereo capture ended before all rotation steps had a left/right frame pair.");
                    }
                    firstPairReceivedAt = std::max(leftQueue.firstReceivedAt, rightQueue.firstReceivedAt);
                    pair.left = std::move(leftQueue.frames.front());
                    pair.right = std::move(rightQueue.frames.front());
                    leftQueue.frames.pop_front();
                    rightQueue.frames.pop_front();
                    leftQueue.bytes -= byteSize(pair.left);
                    rightQueue.bytes -= byteSize(pair.right);
                }
                // 保存独立于扫描命令返回；串口刷新/固件启动等待期间也持续清空队列。
                const auto saveStart = std::chrono::steady_clock::now();
                savePair(pair);
                const auto saveEnd = std::chrono::steady_clock::now();
                const double elapsedMs = std::chrono::duration<double, std::milli>(saveEnd - saveStart).count();
                report.totalSaveMs += elapsedMs;
                report.maxSavePairMs = std::max(report.maxSavePairMs, elapsedMs);
                if (report.savedPairs == 0) {
                    report.firstPairSaveLatencyMs = std::chrono::duration<double, std::milli>(saveEnd - firstPairReceivedAt).count();
                }
                ++report.savedPairs;
            }
        } catch (const std::exception& ex) {
            fail(ex.what());
        } catch (...) {
            fail("Hardware stereo pair saving failed with an unknown exception.");
        }
    };

    std::thread leftThread, rightThread, writerThread;
    try {
        writerThread = std::thread(writePairs);
        leftThread = std::thread(receive, std::move(leftReader), std::ref(leftQueue), "Left");
        rightThread = std::thread(receive, std::move(rightReader), std::ref(rightQueue), "Right");
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&]() { return (readyReaders == 2 && writerReady) || stopped; });
            if (stopped) {
                throw std::runtime_error(error);
            }
        }
        // 接收和保存三路线程已就绪，最后才允许振镜产生扫描脉冲。
        const auto scanStart = std::chrono::steady_clock::now();
        options.cancellation.check();
        startScan();
        report.scanStartMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - scanStart).count();
    } catch (const std::exception& ex) {
        fail(ex.what());
    } catch (...) {
        fail("Hardware stereo capture failed with an unknown exception.");
    }
    if (writerThread.joinable()) {
        writerThread.join();
    }
    if (leftThread.joinable()) {
        leftThread.join();
    }
    if (rightThread.joinable()) {
        rightThread.join();
    }
    if (report.savedPairs != options.expectedFrameCount) {
        fail("Not all expected stereo frame pairs were saved.");
    }
    report.leftReceived = leftQueue.received;
    report.rightReceived = rightQueue.received;
    report.leftPeakQueuedBytes = leftQueue.peakBytes;
    report.rightPeakQueuedBytes = rightQueue.peakBytes;
    report.success = error.empty() && report.leftReceived == options.expectedFrameCount &&
        report.rightReceived == options.expectedFrameCount && report.savedPairs == options.expectedFrameCount;
    report.message = error;
    return report;
}

} // namespace htmsr::app
