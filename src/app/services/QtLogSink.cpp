#include "app/services/QtLogSink.h"

#include "core/Logger.h"

namespace htmsr::app {

QtLogSink::QtLogSink(QObject* parent)
    : QObject(parent)
{
    // 核心 Logger 不依赖 Qt，这里通过 sink 将日志桥接到 Qt 信号。
    Logger::instance().addSink([this](const LogMessage& message) {
        emit messageReceived(message);
    });
}

QtLogSink::~QtLogSink()
{
    // 窗口销毁时清理 sink，避免 Logger 持有已经失效的 QObject 回调。
    Logger::instance().clearSinks();
}

} // namespace htmsr::app
