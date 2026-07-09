#include "core/Logger.h"

namespace htmsr {

Logger& Logger::instance()
{
    // 使用函数内静态变量保证首次使用时初始化。
    static Logger logger;
    return logger;
}

void Logger::addSink(Sink sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::clearSinks()
{
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
}

void Logger::debug(const std::string& module, const std::string& text)
{
    log(LogLevel::Debug, module, text);
}

void Logger::info(const std::string& module, const std::string& text)
{
    log(LogLevel::Info, module, text);
}

void Logger::warning(const std::string& module, const std::string& text)
{
    log(LogLevel::Warning, module, text);
}

void Logger::error(const std::string& module, const std::string& text)
{
    log(LogLevel::Error, module, text);
}

void Logger::log(LogLevel level, const std::string& module, const std::string& text)
{
    std::vector<Sink> sinks;
    {
        // 先复制接收器列表，再在锁外回调，避免 UI 回调中再次写日志造成死锁。
        std::lock_guard<std::mutex> lock(mutex_);
        sinks = sinks_;
    }

    LogMessage message{ level, module, text };
    for (const auto& sink : sinks) {
        if (sink) {
            sink(message);
        }
    }
}

std::string toString(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
        return "Debug";
    case LogLevel::Info:
        return "Info";
    case LogLevel::Warning:
        return "Warning";
    case LogLevel::Error:
        return "Error";
    }
    return "Unknown";
}

} // namespace htmsr
