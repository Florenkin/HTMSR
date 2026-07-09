#pragma once

#include "core/Types.h"

#include <QMetaType>
#include <QObject>

namespace htmsr::app {

class QtLogSink final : public QObject {
    Q_OBJECT

public:
    explicit QtLogSink(QObject* parent = nullptr);
    ~QtLogSink() override;

signals:
    void messageReceived(htmsr::LogMessage message);
};

} // namespace htmsr::app

Q_DECLARE_METATYPE(htmsr::LogMessage)
