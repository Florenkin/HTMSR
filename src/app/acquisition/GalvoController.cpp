#include "app/acquisition/GalvoController.h"

#include "core/Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <winreg.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace htmsr::app {
namespace {

constexpr unsigned char kFrameHeader0 = 0x55;
constexpr unsigned char kFrameHeader1 = 0xAA;
constexpr unsigned char kFrameHeader2 = 0x01;
constexpr unsigned int kNoResponseCommandSettleMs = 30;

enum class GalvoCommand : unsigned char {
    DirectionForward = 0x04,
    DirectionReverse = 0x05,
    ContinuousCapture = 0x09,
    ForwardSpeedSet = 0x0A,
    ForwardSpeedGet = 0x0B,
    ReverseSpeedSet = 0x0C,
    ReverseSpeedGet = 0x0D,
    AutoRotationAngleSet = 0x0E,
    AutoRotationAngleGet = 0x0F,
    StepAngleSet = 0x10,
    StepAngleGet = 0x11,
    SyncModeSet = 0x02,
    SyncModeGet = 0x03,
    LaserOn = 0x1A,
    LaserOff = 0x1B,
    VoltageRangeSet = 0x1C,
    VoltageRangeGet = 0x1D,
    CaptureIntervalSet = 0x16,
    CaptureIntervalGet = 0x17,
    ContinuousWaitSet = 0x18,
    ContinuousWaitGet = 0x19,
    LaserDutySet = 0x29
};

std::string formatBytes(const std::vector<unsigned char>& bytes)
{
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            stream << ' ';
        }
        stream << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return stream.str();
}

unsigned char computeChecksum(const std::vector<unsigned char>& bytes)
{
    unsigned int sum = 0;
    for (unsigned char value : bytes) {
        sum += value;
    }
    return static_cast<unsigned char>(sum & 0xFFu);
}

std::vector<unsigned char> buildNoPayloadCommand(GalvoCommand command)
{
    std::vector<unsigned char> frame = {
        kFrameHeader0,
        kFrameHeader1,
        kFrameHeader2,
        static_cast<unsigned char>(command)
    };
    frame.push_back(computeChecksum(frame));
    return frame;
}

std::vector<unsigned char> buildUint32Command(GalvoCommand command, unsigned int value)
{
    std::vector<unsigned char> frame = {
        kFrameHeader0,
        kFrameHeader1,
        kFrameHeader2,
        static_cast<unsigned char>(command),
        static_cast<unsigned char>(value & 0xFFu),
        static_cast<unsigned char>((value >> 8) & 0xFFu),
        static_cast<unsigned char>((value >> 16) & 0xFFu),
        static_cast<unsigned char>((value >> 24) & 0xFFu)
    };
    frame.push_back(computeChecksum(frame));
    return frame;
}

std::vector<unsigned char> buildPairCommand(GalvoCommand command, unsigned int firstValue, unsigned int secondValue)
{
    std::vector<unsigned char> frame = {
        kFrameHeader0,
        kFrameHeader1,
        kFrameHeader2,
        static_cast<unsigned char>(command),
        static_cast<unsigned char>(firstValue & 0xFFu),
        static_cast<unsigned char>((firstValue >> 8) & 0xFFu),
        static_cast<unsigned char>((firstValue >> 16) & 0xFFu),
        static_cast<unsigned char>((firstValue >> 24) & 0xFFu),
        static_cast<unsigned char>(secondValue & 0xFFu),
        static_cast<unsigned char>((secondValue >> 8) & 0xFFu),
        static_cast<unsigned char>((secondValue >> 16) & 0xFFu),
        static_cast<unsigned char>((secondValue >> 24) & 0xFFu)
    };
    frame.push_back(computeChecksum(frame));
    return frame;
}

size_t expectedResponseSize(const std::vector<unsigned char>& request)
{
    if (request.size() < 4) {
        return 0;
    }

    switch (request[3]) {
    case static_cast<unsigned char>(GalvoCommand::StepAngleGet):
    case static_cast<unsigned char>(GalvoCommand::ForwardSpeedGet):
    case static_cast<unsigned char>(GalvoCommand::ReverseSpeedGet):
        return 13;
    case static_cast<unsigned char>(GalvoCommand::SyncModeGet):
    case static_cast<unsigned char>(GalvoCommand::AutoRotationAngleGet):
    case static_cast<unsigned char>(GalvoCommand::CaptureIntervalGet):
    case static_cast<unsigned char>(GalvoCommand::ContinuousWaitGet):
    case static_cast<unsigned char>(GalvoCommand::VoltageRangeGet):
        return 9;
    default:
        return 0;
    }
}

unsigned int clampInterval(int value)
{
    return static_cast<unsigned int>(std::clamp(value, 1, 255));
}

unsigned int clampAngle(int value)
{
    return static_cast<unsigned int>(std::clamp(value, 0, 40));
}

std::pair<unsigned int, unsigned int> factorizeValue(unsigned int target)
{
    if (target == 0) {
        return { 0u, 0u };
    }

    unsigned int bestFirst = 1;
    unsigned int bestSecond = std::min(target, 255u);
    unsigned int bestError = std::numeric_limits<unsigned int>::max();

    for (unsigned int first = 1; first <= 255; ++first) {
        unsigned int second = static_cast<unsigned int>(std::clamp(
            static_cast<int>(std::lround(static_cast<double>(target) / static_cast<double>(first))),
            1,
            255));
        unsigned int candidate = first * second;
        unsigned int error = candidate > target ? candidate - target : target - candidate;
        if (error < bestError) {
            bestError = error;
            bestFirst = first;
            bestSecond = second;
            if (error == 0) {
                break;
            }
        }
    }
    return { bestFirst, bestSecond };
}

std::pair<unsigned int, unsigned int> encodeStepAngle(double angleDeg)
{
    const long long rawValue = std::llround(angleDeg * 100.0);
    const auto target = static_cast<unsigned int>(std::clamp<long long>(rawValue, 1LL, 65025LL));
    return factorizeValue(target);
}

double decodeStepAngle(unsigned int firstValue, unsigned int secondValue)
{
    return static_cast<double>(firstValue * secondValue) / 100.0;
}

std::pair<unsigned int, unsigned int> encodeSpeedMs(int speedMs)
{
    const auto target = static_cast<unsigned int>(std::clamp(speedMs, 1, 65025));
    return factorizeValue(target);
}

int decodeSpeedMs(unsigned int firstValue, unsigned int secondValue)
{
    return static_cast<int>(firstValue * secondValue);
}

std::pair<unsigned int, unsigned int> encodeLaserDuty(int duty)
{
    const int clampedDuty = std::clamp(duty, 0, 319);
    return { static_cast<unsigned int>(clampedDuty / 256), static_cast<unsigned int>(clampedDuty % 256) };
}

double clampVoltage(double voltageV)
{
    return std::clamp(voltageV, 0.0, 25.5);
}

unsigned int encodeVoltage(double voltageV)
{
    const long long rawValue = std::llround(clampVoltage(voltageV) * 10.0);
    return static_cast<unsigned int>(std::clamp<long long>(rawValue, 0LL, 255LL));
}

double decodeVoltage(unsigned int rawValue)
{
    return static_cast<double>(rawValue) / 10.0;
}

bool startsWithHeader(const std::vector<unsigned char>& bytes)
{
    return bytes.size() >= 4 &&
        bytes[0] == kFrameHeader0 &&
        bytes[1] == kFrameHeader1 &&
        bytes[2] == kFrameHeader2;
}

bool hasValidChecksum(const std::vector<unsigned char>& bytes, size_t offset, size_t length)
{
    if (length < 5 || offset > bytes.size() || length > bytes.size() - offset) {
        return false;
    }

    unsigned int sum = 0;
    for (size_t index = offset; index + 1 < offset + length; ++index) {
        sum += bytes[index];
    }
    return static_cast<unsigned char>(sum & 0xFFu) == bytes[offset + length - 1];
}

bool tryExtractExpectedResponse(
    const std::vector<unsigned char>& response,
    const std::vector<unsigned char>& request,
    size_t expectedBytes,
    std::vector<unsigned char>& frame)
{
    frame.clear();
    if (request.size() < 4 || expectedBytes < 5 || response.size() < expectedBytes) {
        return false;
    }

    for (size_t offset = 0; offset + expectedBytes <= response.size(); ++offset) {
        if (response[offset] != kFrameHeader0 ||
            response[offset + 1] != kFrameHeader1 ||
            response[offset + 2] != kFrameHeader2 ||
            response[offset + 3] != request[3]) {
            continue;
        }
        if (!hasValidChecksum(response, offset, expectedBytes)) {
            continue;
        }

        frame.assign(response.begin() + static_cast<std::ptrdiff_t>(offset),
            response.begin() + static_cast<std::ptrdiff_t>(offset + expectedBytes));
        return true;
    }
    return false;
}

bool tryParseSingleUint32Response(const std::vector<unsigned char>& response, unsigned int& value)
{
    if (!startsWithHeader(response) || response.size() < 9 || !hasValidChecksum(response, 0, 9)) {
        return false;
    }
    value = static_cast<unsigned int>(response[4]) |
        (static_cast<unsigned int>(response[5]) << 8) |
        (static_cast<unsigned int>(response[6]) << 16) |
        (static_cast<unsigned int>(response[7]) << 24);
    return true;
}

bool tryParsePairResponse(const std::vector<unsigned char>& response, unsigned int& firstValue, unsigned int& secondValue)
{
    if (!startsWithHeader(response) || response.size() < 13 || !hasValidChecksum(response, 0, 13)) {
        return false;
    }
    firstValue = static_cast<unsigned int>(response[4]) |
        (static_cast<unsigned int>(response[5]) << 8) |
        (static_cast<unsigned int>(response[6]) << 16) |
        (static_cast<unsigned int>(response[7]) << 24);
    secondValue = static_cast<unsigned int>(response[8]) |
        (static_cast<unsigned int>(response[9]) << 8) |
        (static_cast<unsigned int>(response[10]) << 16) |
        (static_cast<unsigned int>(response[11]) << 24);
    return true;
}

GalvoCommandResult makeDisconnectedResult(const std::string& message)
{
    GalvoCommandResult result;
    result.success = false;
    result.message = message;
    return result;
}

#ifdef _WIN32
std::wstring toWide(const std::string& value)
{
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return std::wstring(value.begin(), value.end());
    }

    std::wstring wide(size, L'\0');
    const int written = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), size);
    if (written <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return wide;
}

std::string normalizePortName(const std::string& portName)
{
    if (portName.empty()) {
        return portName;
    }
    if (portName.rfind("\\\\.\\", 0) == 0) {
        return portName;
    }
    return "\\\\.\\" + portName;
}

std::string serialConnectionError(const char* action, const std::string& portName, DWORD errorCode)
{
    std::string message = std::string(action) + "：" + portName + "。";
    if (errorCode == ERROR_FILE_NOT_FOUND || errorCode == ERROR_PATH_NOT_FOUND) {
        message += "系统未识别到此串口。请检查振镜 USB 数据线、控制器供电和驱动，再刷新设备自动识别串口。";
    } else if (errorCode == ERROR_ACCESS_DENIED || errorCode == ERROR_SHARING_VIOLATION) {
        message += "串口可能被占用或访问被拒绝。请关闭串口调试助手、厂家控制软件和其他 HTMSR 实例。";
    }
    message += " Windows 错误 " + std::to_string(errorCode);
    wchar_t buffer[512]{};
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, 0, buffer, static_cast<DWORD>(std::size(buffer)), nullptr);
    if (length > 0) {
        std::wstring systemMessage(buffer, length);
        while (!systemMessage.empty() && (systemMessage.back() == L'\r' ||
            systemMessage.back() == L'\n' || systemMessage.back() == L' ')) {
            systemMessage.pop_back();
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, systemMessage.data(),
            static_cast<int>(systemMessage.size()), nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            std::string utf8(bytes, '\0');
            WideCharToMultiByte(CP_UTF8, 0, systemMessage.data(),
                static_cast<int>(systemMessage.size()), utf8.data(), bytes, nullptr, nullptr);
            message += "：" + utf8;
        }
    }
    return message;
}
#endif

} // namespace

struct SerialGalvoController::Impl {
    GalvoScanConfig config;
    bool connected = false;
    std::string lastError;
    GalvoSyncMode syncMode = GalvoSyncMode::Sync;
    int captureIntervalMs = 30;
    int continuousCaptureWaitMs = 50;
    double stepAngleDeg = 0.2;
    int autoRotationAngleDeg = 22;
    int forwardSpeedMs = 10;
    int reverseSpeedMs = 10;
    int laserDuty = 100;
    double voltageRangeV = 7.0;
    GalvoScanDirection direction = GalvoScanDirection::Forward;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#endif
};

/*
    函数功能：枚举当前系统可选的串口名称
    输入：
        无
    输出：
        返回值：按名称排序后的 COM 端口列表
*/
std::vector<std::string> enumerateSerialPortNames()
{
    std::vector<std::string> ports;
#ifdef _WIN32
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        char valueName[256];
        BYTE data[256];
        DWORD valueNameSize = 0;
        DWORD dataSize = 0;
        DWORD index = 0;
        while (true) {
            valueNameSize = sizeof(valueName);
            dataSize = sizeof(data);
            DWORD type = 0;
            const LONG result = RegEnumValueA(
                key,
                index++,
                valueName,
                &valueNameSize,
                nullptr,
                &type,
                data,
                &dataSize);
            if (result != ERROR_SUCCESS) {
                break;
            }
            if (type == REG_SZ && dataSize > 1) {
                ports.emplace_back(reinterpret_cast<const char*>(data));
            }
        }
        RegCloseKey(key);
    }
#endif
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

/*
    函数功能：构造串口振镜控制器
    输入：
        config：振镜串口与扫描参数配置
    输出：
        无（构造后保存配置，尚未真正打开串口）
*/
SerialGalvoController::SerialGalvoController(GalvoScanConfig config)
    : impl_(std::make_unique<Impl>())
{
    impl_->config = std::move(config);
    impl_->syncMode = impl_->config.syncMode;
    impl_->captureIntervalMs = impl_->config.captureIntervalMs;
    impl_->continuousCaptureWaitMs = impl_->config.continuousCaptureWaitMs;
    impl_->stepAngleDeg = impl_->config.stepAngleDeg;
    impl_->autoRotationAngleDeg = impl_->config.autoRotationAngleDeg;
    impl_->forwardSpeedMs = impl_->config.forwardSpeedMs;
    impl_->reverseSpeedMs = impl_->config.reverseSpeedMs;
    impl_->laserDuty = impl_->config.laserDuty;
    impl_->voltageRangeV = impl_->config.voltageRangeV;
    impl_->direction = impl_->config.direction;
}

SerialGalvoController::~SerialGalvoController()
{
    disconnect();
}

bool SerialGalvoController::connect()
{
    impl_->lastError.clear();
    if (impl_->config.portName.empty()) {
        impl_->lastError = "未识别到唯一的振镜串口。请连接振镜 USB 串口并刷新设备；多个串口时请暂时断开其他串口设备。";
        Logger::instance().error("Galvo", impl_->lastError);
        return false;
    }
#ifndef _WIN32
    impl_->lastError = "Serial galvo controller is only implemented for Windows.";
    Logger::instance().error("Galvo", impl_->lastError);
    return false;
#else
    if (impl_->connected) {
        return true;
    }
    const auto failConnection = [&](const char* action, DWORD errorCode) {
        impl_->lastError = serialConnectionError(action, impl_->config.portName, errorCode);
        Logger::instance().error("Galvo", impl_->lastError);
        disconnect();
        return false;
    };

    const std::string portName = normalizePortName(impl_->config.portName);
    impl_->handle = CreateFileW(
        toWide(portName).c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (impl_->handle == INVALID_HANDLE_VALUE) {
        const DWORD errorCode = GetLastError();
        return failConnection("无法打开振镜串口", errorCode);
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(impl_->handle, &dcb)) {
        return failConnection("读取振镜串口配置失败", GetLastError());
    }

    dcb.BaudRate = static_cast<DWORD>(impl_->config.baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(impl_->handle, &dcb)) {
        return failConnection("设置振镜串口配置失败", GetLastError());
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(impl_->config.commandTimeoutMs);
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(impl_->config.commandTimeoutMs);
    timeouts.WriteTotalTimeoutMultiplier = 0;
    if (!SetCommTimeouts(impl_->handle, &timeouts)) {
        return failConnection("设置振镜串口超时失败", GetLastError());
    }

    PurgeComm(impl_->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    impl_->connected = true;
    Logger::instance().debug("Galvo", "Connected galvo serial port: " + impl_->config.portName);
    return true;
#endif
}

void SerialGalvoController::disconnect()
{
#ifdef _WIN32
    if (impl_ && impl_->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->handle);
        impl_->handle = INVALID_HANDLE_VALUE;
    }
#endif
    if (impl_) {
        impl_->connected = false;
    }
}

bool SerialGalvoController::isConnected() const
{
    return impl_ && impl_->connected;
}

std::string SerialGalvoController::lastError() const
{
    return impl_ ? impl_->lastError : std::string{};
}

GalvoCommandResult sendAndReceive(
#ifdef _WIN32
    HANDLE handle,
#endif
    const GalvoScanConfig& config,
    bool connected,
    const std::vector<unsigned char>& request,
    bool expectResponse)
{
    GalvoCommandResult result;
    result.request = request;

#ifndef _WIN32
    result.success = false;
    result.message = "Serial galvo controller is only implemented for Windows.";
    return result;
#else
    if (!connected || handle == INVALID_HANDLE_VALUE) {
        result.success = false;
        result.message = "Galvo serial port is not connected.";
        return result;
    }

    // Clear bytes left by an earlier command before sending a request that
    // expects a response. Clearing RX after WriteFile creates a race: a fast
    // controller response may already be queued and would be discarded.
    if (expectResponse) {
        PurgeComm(handle, PURGE_RXCLEAR);
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(handle, request.data(), static_cast<DWORD>(request.size()), &bytesWritten, nullptr) || bytesWritten != request.size()) {
        result.success = false;
        result.message = "Failed to write galvo command.";
        Logger::instance().error("Galvo", result.message + " Request=" + formatBytes(request));
        return result;
    }

    FlushFileBuffers(handle);
    Logger::instance().debug("Galvo", "Sent command: " + formatBytes(request));

    if (!expectResponse) {
        Sleep(kNoResponseCommandSettleMs);
        result.success = true;
        result.message = "Command sent.";
        return result;
    }

    const ULONGLONG startTick = GetTickCount64();
    const size_t expectedBytes = expectedResponseSize(request);
    std::array<unsigned char, 64> buffer{};
    std::vector<unsigned char> matchedResponse;
    while (GetTickCount64() - startTick < static_cast<ULONGLONG>(config.commandTimeoutMs)) {
        DWORD errors = 0;
        COMSTAT stat{};
        if (!ClearCommError(handle, &errors, &stat)) {
            break;
        }

        if (stat.cbInQue > 0) {
            const DWORD toRead = std::min<DWORD>(stat.cbInQue, static_cast<DWORD>(buffer.size()));
            DWORD bytesRead = 0;
            if (ReadFile(handle, buffer.data(), toRead, &bytesRead, nullptr) && bytesRead > 0) {
                result.response.insert(result.response.end(), buffer.begin(), buffer.begin() + bytesRead);
            }
        }

        if (!result.response.empty() &&
            (expectedBytes == 0 || tryExtractExpectedResponse(result.response, request, expectedBytes, matchedResponse))) {
            break;
        }

        Sleep(10);
    }

    if (expectedBytes > 0) {
        if (tryExtractExpectedResponse(result.response, request, expectedBytes, matchedResponse)) {
            result.response = matchedResponse;
            result.success = true;
        } else {
            result.success = false;
        }
    } else {
        result.success = !result.response.empty();
    }
    result.message = result.success ? "Command response received." : "No response received before timeout.";
    Logger::instance().log(
        result.success ? LogLevel::Info : LogLevel::Warning,
        "Galvo",
        result.message + " Request=" + formatBytes(request) +
            (result.response.empty() ? "" : " Response=" + formatBytes(result.response)));
    return result;
#endif
}

GalvoCommandResult SerialGalvoController::setSyncMode(GalvoSyncMode mode)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildUint32Command(GalvoCommand::SyncModeSet, mode == GalvoSyncMode::Sync ? 1u : 0u),
        false);
    if (result.success) {
        impl_->syncMode = mode;
    }
    return result;
}

GalvoCommandResult SerialGalvoController::getSyncMode(GalvoSyncMode& mode)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::SyncModeGet),
        true);
    unsigned int rawValue = 0;
    if (result.success && tryParseSingleUint32Response(result.response, rawValue)) {
        mode = rawValue == 0 ? GalvoSyncMode::Async : GalvoSyncMode::Sync;
        impl_->syncMode = mode;
    } else {
        if (result.success) {
            result.success = false;
            result.message = "Invalid sync-mode response payload; cached state is not a verified device readback.";
        }
        mode = impl_->syncMode;
    }
    return result;
}

GalvoCommandResult SerialGalvoController::setCaptureIntervalMs(int intervalMs)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildUint32Command(GalvoCommand::CaptureIntervalSet, clampInterval(intervalMs)),
        false);
    if (result.success) {
        impl_->captureIntervalMs = static_cast<int>(clampInterval(intervalMs));
    }
    return result;
}

GalvoCommandResult SerialGalvoController::getCaptureIntervalMs(int& intervalMs)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::CaptureIntervalGet),
        true);
    unsigned int rawValue = 0;
    if (result.success && tryParseSingleUint32Response(result.response, rawValue)) {
        impl_->captureIntervalMs = static_cast<int>(rawValue);
    }
    intervalMs = impl_->captureIntervalMs;
    return result;
}

GalvoCommandResult SerialGalvoController::setContinuousCaptureWaitMs(int intervalMs)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildUint32Command(GalvoCommand::ContinuousWaitSet, clampInterval(intervalMs)),
        false);
    if (result.success) {
        impl_->continuousCaptureWaitMs = static_cast<int>(clampInterval(intervalMs));
    }
    return result;
}

GalvoCommandResult SerialGalvoController::getContinuousCaptureWaitMs(int& intervalMs)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::ContinuousWaitGet),
        true);
    unsigned int rawValue = 0;
    if (result.success && tryParseSingleUint32Response(result.response, rawValue)) {
        impl_->continuousCaptureWaitMs = static_cast<int>(rawValue);
    }
    intervalMs = impl_->continuousCaptureWaitMs;
    return result;
}

GalvoCommandResult SerialGalvoController::setStepAngle(double angleDeg)
{
    const auto [firstValue, secondValue] = encodeStepAngle(angleDeg);
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildPairCommand(GalvoCommand::StepAngleSet, firstValue, secondValue),
        false);
    if (result.success) {
        impl_->stepAngleDeg = decodeStepAngle(firstValue, secondValue);
    }
    return result;
}

GalvoCommandResult SerialGalvoController::getStepAngle(double& angleDeg)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::StepAngleGet),
        true);
    unsigned int firstValue = 0;
    unsigned int secondValue = 0;
    if (result.success && tryParsePairResponse(result.response, firstValue, secondValue)) {
        impl_->stepAngleDeg = decodeStepAngle(firstValue, secondValue);
    } else if (result.success) {
        result.success = false;
        result.message = "Invalid step-angle response payload; cached angle is not a verified device readback.";
    }
    angleDeg = impl_->stepAngleDeg;
    return result;
}

GalvoCommandResult SerialGalvoController::setAutoRotationAngle(int angleDeg)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildUint32Command(GalvoCommand::AutoRotationAngleSet, clampAngle(angleDeg)),
        false);
    if (result.success) {
        impl_->autoRotationAngleDeg = static_cast<int>(clampAngle(angleDeg));
    }
    return result;
}

GalvoCommandResult SerialGalvoController::getAutoRotationAngle(int& angleDeg)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::AutoRotationAngleGet),
        true);
    unsigned int rawValue = 0;
    if (result.success && tryParseSingleUint32Response(result.response, rawValue)) {
        impl_->autoRotationAngleDeg = static_cast<int>(rawValue);
    } else if (result.success) {
        result.success = false;
        result.message = "Invalid total-angle response payload; cached angle is not a verified device readback.";
    }
    angleDeg = impl_->autoRotationAngleDeg;
    return result;
}

GalvoCommandResult SerialGalvoController::setForwardSpeedMs(int speedMs)
{
    const auto [firstValue, secondValue] = encodeSpeedMs(speedMs);
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildPairCommand(GalvoCommand::ForwardSpeedSet, firstValue, secondValue),
        false);
    if (result.success) {
        impl_->forwardSpeedMs = decodeSpeedMs(firstValue, secondValue);
    }
    return result;
}

GalvoCommandResult SerialGalvoController::getForwardSpeedMs(int& speedMs)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::ForwardSpeedGet),
        true);
    unsigned int firstValue = 0;
    unsigned int secondValue = 0;
    if (result.success && tryParsePairResponse(result.response, firstValue, secondValue)) {
        impl_->forwardSpeedMs = decodeSpeedMs(firstValue, secondValue);
    }
    speedMs = impl_->forwardSpeedMs;
    return result;
}

GalvoCommandResult SerialGalvoController::setReverseSpeedMs(int speedMs)
{
    const auto [firstValue, secondValue] = encodeSpeedMs(speedMs);
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildPairCommand(GalvoCommand::ReverseSpeedSet, firstValue, secondValue),
        false);
    if (result.success) {
        impl_->reverseSpeedMs = decodeSpeedMs(firstValue, secondValue);
    }
    return result;
}

GalvoCommandResult SerialGalvoController::getReverseSpeedMs(int& speedMs)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::ReverseSpeedGet),
        true);
    unsigned int firstValue = 0;
    unsigned int secondValue = 0;
    if (result.success && tryParsePairResponse(result.response, firstValue, secondValue)) {
        impl_->reverseSpeedMs = decodeSpeedMs(firstValue, secondValue);
    }
    speedMs = impl_->reverseSpeedMs;
    return result;
}

GalvoCommandResult SerialGalvoController::setLaserDuty(int duty)
{
    const auto [firstValue, secondValue] = encodeLaserDuty(duty);
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildPairCommand(GalvoCommand::LaserDutySet, firstValue, secondValue),
        false);
    if (result.success) {
        impl_->laserDuty = std::clamp(duty, 0, 319);
    }
    return result;
}

GalvoCommandResult SerialGalvoController::setVoltageRange(double voltageV)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildUint32Command(GalvoCommand::VoltageRangeSet, encodeVoltage(voltageV)),
        false);
    if (result.success) {
        impl_->voltageRangeV = clampVoltage(voltageV);
    }
    return result;
}

GalvoCommandResult SerialGalvoController::getVoltageRange(double& voltageV)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::VoltageRangeGet),
        true);
    unsigned int rawValue = 0;
    if (result.success && tryParseSingleUint32Response(result.response, rawValue)) {
        impl_->voltageRangeV = decodeVoltage(rawValue);
    }
    voltageV = impl_->voltageRangeV;
    return result;
}

GalvoCommandResult SerialGalvoController::setScanDirection(GalvoScanDirection direction)
{
    auto result = sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(direction == GalvoScanDirection::Forward ? GalvoCommand::DirectionForward : GalvoCommand::DirectionReverse),
        false);
    if (result.success) {
        impl_->direction = direction;
    }
    return result;
}

GalvoCommandResult SerialGalvoController::laserOn()
{
    return sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::LaserOn),
        false);
}

GalvoCommandResult SerialGalvoController::laserOff()
{
    return sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::LaserOff),
        false);
}

GalvoCommandResult SerialGalvoController::startContinuousCapture()
{
    return sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        buildNoPayloadCommand(GalvoCommand::ContinuousCapture),
        false);
}

GalvoCommandResult SerialGalvoController::sendRawCommand(const std::vector<unsigned char>& command, bool expectResponse)
{
    if (command.empty()) {
        GalvoCommandResult result;
        result.success = false;
        result.message = "Raw galvo command is empty.";
        return result;
    }

    return sendAndReceive(
#ifdef _WIN32
        impl_->handle,
#endif
        impl_->config,
        impl_->connected,
        command,
        expectResponse);
}

} // namespace htmsr::app
