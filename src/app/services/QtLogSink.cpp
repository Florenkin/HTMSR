#include "app/services/QtLogSink.h"

#include "core/Logger.h"

namespace htmsr::app {

QtLogSink::QtLogSink(QObject* parent)
    : QObject(parent)
{
    Logger::instance().addSink([this](const LogMessage& message) {
        emit messageReceived(message);
    });
}

QtLogSink::~QtLogSink()
{
    Logger::instance().clearSinks();
}

} // namespace htmsr::app
