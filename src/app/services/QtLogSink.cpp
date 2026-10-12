#include "app/services/QtLogSink.h"

#include "core/Logger.h"

namespace htmsr::app {

QtLogSink::QtLogSink(QObject* parent)
    : QObject(parent)
{
    // 核心 Logger 不依赖 Qt，这里通过 sink 将日志桥接到 Qt 信号。
    sinkId_ = Logger::instance().addSink([this](const LogMessage& message) {
        if (message.level != LogLevel::Debug) {
            emit messageReceived(message);
        }
    });
}

QtLogSink::~QtLogSink()
{
    Logger::instance().removeSink(sinkId_);
}

} // namespace htmsr::app
