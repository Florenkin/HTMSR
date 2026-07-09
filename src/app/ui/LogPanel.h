#pragma once

#include "core/Types.h"

#include <QTableWidget>
#include <QWidget>

namespace htmsr::app {

class LogPanel final : public QWidget {
    Q_OBJECT

public:
    explicit LogPanel(QWidget* parent = nullptr);

public slots:
    void appendMessage(const htmsr::LogMessage& message);
    void clear();

private:
    QTableWidget* table_ = nullptr;
};

} // namespace htmsr::app
