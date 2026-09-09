#pragma once

#include "core/Types.h"

#include <QMetaType>
#include <QObject>

namespace htmsr::app {

class QtLogSink final : public QObject {
    Q_OBJECT

public:
    /*
        函数功能：构造 Qt 日志接收器，将核心 Logger 日志转发为 Qt 信号
        输入：
            parent：Qt 父对象
        输出：
            无
    */
    explicit QtLogSink(QObject* parent = nullptr);
    ~QtLogSink() override;

signals:
    // 收到核心日志后发出该信号，由 UI 日志面板在主线程中显示。
    void messageReceived(htmsr::LogMessage message);
};

} // namespace htmsr::app

Q_DECLARE_METATYPE(htmsr::LogMessage)
