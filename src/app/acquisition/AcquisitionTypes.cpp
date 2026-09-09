#include "app/acquisition/AcquisitionTypes.h"

namespace htmsr::app {

/*
    函数功能：将相机状态枚举转换为便于日志和界面显示的字符串
    输入：
        state：相机设备当前状态
    输出：
        返回值：状态对应的英文字符串
*/
std::string toString(CameraState state)
{
    // 根据不同的相机状态返回统一的可读文本，供 UI 和日志直接复用。
    switch (state) {
    case CameraState::Disconnected:
        return "Disconnected";
    case CameraState::Connected:
        return "Connected";
    case CameraState::Streaming:
        return "Streaming";
    case CameraState::Error:
        return "Error";
    }

    // 兜底返回，避免后续扩展新枚举值但未同步更新这里时出现空返回。
    return "Unknown";
}

/*
    函数功能：将振镜同步模式枚举转换为便于日志和界面显示的字符串
    输入：
        mode：振镜与相机的同步模式
    输出：
        返回值：同步模式对应的英文字符串
*/
std::string toString(GalvoSyncMode mode)
{
    switch (mode) {
    case GalvoSyncMode::Async:
        return "Async";
    case GalvoSyncMode::Sync:
        return "Sync";
    }
    return "Unknown";
}

/*
    函数功能：将振镜扫描方向枚举转换为便于日志和界面显示的字符串
    输入：
        direction：振镜扫描方向
    输出：
        返回值：扫描方向对应的英文字符串
*/
std::string toString(GalvoScanDirection direction)
{
    switch (direction) {
    case GalvoScanDirection::Forward:
        return "Forward";
    case GalvoScanDirection::Reverse:
        return "Reverse";
    }
    return "Unknown";
}

} // namespace htmsr::app
