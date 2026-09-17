#pragma once

#include "app/acquisition/AcquisitionTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace htmsr::app {

// SDK 缓冲释放后图像仍独立持有内存；帧号保留用于检查接收序列，不能用保存序号代替。
struct CameraFrame {
    cv::Mat image;
    std::uint32_t frameNumber = 0;
    std::uint32_t triggerIndex = 0;
    std::uint32_t lostPacketCount = 0;
    std::int64_t hostTimestamp = 0;
    bool metadataValid = false;
};

struct HardwareStereoFrame {
    CameraFrame left;
    CameraFrame right;
    int frameIndex = 0;
};

struct HardwareCaptureOptions {
    int expectedFrameCount = 0;
    int frameTimeoutMs = 3000;
    int pollTimeoutMs = 100;
    int extraFrameCheckMs = 100;
    std::size_t maxQueuedBytesPerCamera = 128 * 1024 * 1024;
};

struct HardwareCaptureReport {
    int leftReceived = 0;
    int rightReceived = 0;
    int savedPairs = 0;
    double scanStartMs = 0.0;
    double firstPairSaveLatencyMs = 0.0;
    double totalSaveMs = 0.0;
    double maxSavePairMs = 0.0;
    std::size_t leftPeakQueuedBytes = 0;
    std::size_t rightPeakQueuedBytes = 0;
    bool success = false;
    std::string message;
};

// 两路接收及一路配对保存线程在扫描前就绪；扫描命令阻塞不会阻塞保存。
// 队列满、帧号跳变或缺帧均失败，不丢弃后继续错配。
class HardwareTriggeredStereoCapture {
public:
    using FrameReader = std::function<CameraFrame(int timeoutMs)>;
    using PairWriter = std::function<void(const HardwareStereoFrame&)>;

    HardwareCaptureReport capture(
        const HardwareCaptureOptions& options,
        FrameReader leftReader,
        FrameReader rightReader,
        std::function<void()> startScan,
        PairWriter savePair) const;
};

} // namespace htmsr::app
