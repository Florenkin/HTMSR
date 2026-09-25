#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

namespace htmsr {

class OperationCancelled : public std::runtime_error {
public:
    OperationCancelled() : std::runtime_error("操作已取消。") {}
};

// 值拷贝共享状态；不依赖窗口生命周期，线程间只通过原子标记通信。
class CancellationToken {
public:
    void request() const { requested_->store(true); }
    bool requested() const { return requested_->load(); }
    void check() const { if (requested()) throw OperationCancelled(); }
    void wait(int milliseconds) const
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            check();
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min<long long>(remaining, 20)));
        } while (true);
    }
private:
    std::shared_ptr<std::atomic_bool> requested_ = std::make_shared<std::atomic_bool>(false);
};

} // namespace htmsr
