#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"

#include <chrono>
#include <utility>

namespace netx::core {
namespace details {
template <typename Duration>
class SleepAwaiter {
  private:
    Duration dora{};

  public:
    explicit SleepAwaiter(Duration &&duration) : dora(std::move(duration)) {
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <Promise P>
    void await_suspend(std::coroutine_handle<P> coro) const noexcept {
        EventLoop::loop().call_after(dora, coro.promise());
    }

    void await_resume() const noexcept {
    }
};

/// 立即启动的实现层：签名里带 NoWaitAtInitialSuspend，promise 的
/// initial_suspend 就不挂起，于是调用即在当前线程上把定时器挂好。
template <typename Rep, typename Period>
Task<Expected<>> sleep(NoWaitAtInitialSuspend,
                       std::chrono::duration<Rep, Period> duration) {
    co_await SleepAwaiter{std::move(duration)};
    co_return {};
}
} // namespace details

/// 立即开始计时：一调用就把定时器登记上去，不必先被人 co_await。
template <typename Rep, typename Period>
Task<Expected<>> sleep_started(std::chrono::duration<Rep, Period> duration) {
    return details::sleep(details::no_wait_at_initial_suspend,
                          std::move(duration));
}

/// 惰性版本：由调用方决定什么时候开始计时（交给事件循环/被 await 时才起算）。
template <typename Rep, typename Period>
Task<Expected<>> sleep(std::chrono::duration<Rep, Period> duration) {
    co_await details::SleepAwaiter{std::move(duration)};
    co_return {};
}
} // namespace netx::core