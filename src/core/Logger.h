#pragma once

#include "core/Types.h"

#include <functional>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>
#include <utility>

namespace htmsr {

class Logger {
public:
    using Sink = std::function<void(const LogMessage&)>;
    using SinkId = std::size_t;

    /*
        函数功能：获取全局日志对象
        输入：
            无
        输出：
            返回值：Logger 单例引用
    */
    static Logger& instance();

    /*
        函数功能：添加一个日志接收器，用于将日志转发到 UI 或文件
        输入：
            sink：日志接收回调函数
        输出：
            无
    */
    SinkId addSink(Sink sink);
    // 仅移除指定接收器，不影响文件或其他窗口的日志。
    void removeSink(SinkId id);

    /*
        函数功能：清空当前注册的所有日志接收器
        输入：
            无
        输出：
            无
    */
    void clearSinks();

    void debug(const std::string& module, const std::string& text);
    void info(const std::string& module, const std::string& text);
    void warning(const std::string& module, const std::string& text);
    void error(const std::string& module, const std::string& text);

    /*
        函数功能：生成一条指定级别、模块和内容的日志，并分发给所有接收器
        输入：
            level：日志级别
            module：日志所属模块
            text：日志内容
        输出：
            无
    */
    void log(LogLevel level, const std::string& module, const std::string& text);

private:
    Logger() = default;

    std::mutex mutex_;
    std::vector<std::pair<SinkId, Sink>> sinks_;
    SinkId nextSinkId_ = 1;
};

/*
    函数功能：将日志级别枚举转换为字符串
    输入：
        level：日志级别
    输出：
        返回值：日志级别对应的字符串
*/
std::string toString(LogLevel level);

} // namespace htmsr
