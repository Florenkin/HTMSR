#pragma once

#include <QObject>
#include <QTimer>
#include <functional>
#include <utility>

namespace htmsr::app {

// 合并连续编辑；关闭窗口时 flush 提交最后一次编辑。
class ConfigAutoSave final : public QObject {
public:
    ConfigAutoSave(std::function<void()> save, QObject* parent = nullptr, int delayMs = 500)
        : QObject(parent), save_(std::move(save))
    {
        timer_.setSingleShot(true);
        timer_.setInterval(delayMs);
        connect(&timer_, &QTimer::timeout, this, [this]() { save_(); });
    }
    void schedule() { timer_.start(); }
    void flush() { timer_.stop(); save_(); }

private:
    QTimer timer_;
    std::function<void()> save_;
};

} // namespace htmsr::app
