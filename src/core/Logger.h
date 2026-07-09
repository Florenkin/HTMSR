#pragma once

#include "core/Types.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace htmsr {

class Logger {
public:
    using Sink = std::function<void(const LogMessage&)>;

    static Logger& instance();

    void addSink(Sink sink);
    void clearSinks();

    void debug(const std::string& module, const std::string& text);
    void info(const std::string& module, const std::string& text);
    void warning(const std::string& module, const std::string& text);
    void error(const std::string& module, const std::string& text);
    void log(LogLevel level, const std::string& module, const std::string& text);

private:
    Logger() = default;

    std::mutex mutex_;
    std::vector<Sink> sinks_;
};

std::string toString(LogLevel level);

} // namespace htmsr
