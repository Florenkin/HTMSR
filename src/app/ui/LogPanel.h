#pragma once

#include "core/Types.h"

#include <QTableWidget>
#include <QWidget>

namespace htmsr::app {

class LogPanel final : public QWidget {
    Q_OBJECT

public:
    /*
        函数功能：构造日志显示面板
        输入：
            parent：Qt 父控件
        输出：
            无
    */
    explicit LogPanel(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    /*
        函数功能：向日志表追加一条日志消息
        输入：
            message：核心 Logger 产生的日志消息
        输出：
            无
    */
    void appendMessage(const htmsr::LogMessage& message);
    // 清空日志表。
    void clear();

private:
    QTableWidget* table_ = nullptr;
    bool scrollPending_ = false;
};

} // namespace htmsr::app
