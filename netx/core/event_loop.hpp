#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/epoller.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/handle.hpp"
#include <algorithm>
#include <chrono>
#include <iterator>
#include <list>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

namespace netx::core::details {
class EventLoop;

/// 等待一个 fd 上的事件。
class EventAwaiter {
  private:
    EventLoop &loop_;
    Event event_{};
    bool registered_{false};
    Expected<> exp_;

  public:
    explicit EventAwaiter(EventLoop &loop, const Event &event)
        : loop_(loop), event_(event) {
    }

    ~EventAwaiter() {
        (void)reset();
    }

    EventAwaiter(EventAwaiter &&other) noexcept
        : loop_(other.loop_), event_(other.event_),
          registered_(std::exchange(other.registered_, false)),
          exp_(std::move(other.exp_)) {
    }

    EventAwaiter &operator=(EventAwaiter &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        (void)reset();
        event_ = other.event_;
        registered_ = std::exchange(other.registered_, false);
        exp_ = std::move(other.exp_);
        return *this;
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <Promise P>
    bool await_suspend(std::coroutine_handle<P> coro);

    Expected<> await_resume() noexcept {
        return std::move(exp_);
    }

    /// 注销 fd 并清掉循环里的 kEvent 记录；可重复调用
    Expected<> reset() noexcept;

    /// 由 EventLoop::cancel() 调用：注销 fd 并把结果置为 Cancelled。
    void on_cancel() noexcept {
        (void)reset();
        exp_ = make_error_to_unexpected(Error::Cancelled);
    }
};

class EventLoop {
  public:
    using EventAwaiter = netx::core::details::EventAwaiter;

    friend class netx::core::details::EventAwaiter;

    static_assert(Awaiter<EventAwaiter>);

  private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;
    using ms = std::chrono::milliseconds;

    using Kind = Handle::SlotKind;

  public:
    [[nodiscard]] bool stopped() const noexcept {
        return pending_ == 0;
    }

    /// 该 handle 当前是否有挂起记录（就绪 / 定时 / 等待事件）
    [[nodiscard]] bool pending(const Handle &handle) const noexcept {
        return handle.slot_kind != Kind::kNone;
    }

    /// 尽快执行一次。幂等：已取消或已有挂起记录的 handle 不会重复入队。
    void call_soon(Handle &handle) {
        if (handle.cancelled || handle.slot_kind != Kind::kNone) {
            return;
        }

        enqueue_ready(handle);
    }

    /// 强制入队
    ///
    /// 取消路径必须能把等待者叫回来：被取消的一方要醒一次，才能读到
    /// Cancelled 结果、并把取消传给自己的等待者（见 CoroHandle::wake）。
    /// 用 call_soon 会被挡下
    void wake(Handle &handle) {
        enqueue_ready(handle);
    }

    /// 在指定时刻执行。幂等规则同 call_soon，因此同一个 handle 不会同时存在
    /// 多条挂起记录。
    void call_at(TimePoint tp, Handle &handle) {
        if (handle.cancelled || handle.slot_kind != Kind::kNone) {
            return;
        }

        auto [it, _] =
            scheduled_.insert({tp, {.handle = &handle, .id = handle.id}});
        mark(handle, Kind::kTimer);
        handle.slot.timer = it;
    }

    template <typename Rep, typename Period>
    void call_after(std::chrono::duration<Rep, Period> duration,
                    Handle &handle) {
        // 饱和加法：duration 大到会把 time_point 顶溢出时钳到最大时刻。
        // 直接写 now + duration 会回绕成负数（nanoseconds::max() 就够触发），
        // 截止时间落到过去，定时器立刻触发 —— 而调用方的意思是"很久以后"。
        const Clock::time_point now = Clock::now();
        const Clock::duration room = Clock::time_point::max() - now;
        const Clock::duration want =
            std::chrono::duration_cast<Clock::duration>(duration);
        call_at(now + std::min(want, room), handle);
    }

    /// 取消，并沿等待链向下传递。幂等，重复调用无副作用。
    ///
    /// 一个 handle 可能同时有两重身份：自己的挂起记录，以及"正等谁" 。
    /// 顺序是先处理自己，再沿 waiting_on 下行：
    ///
    /// - 就绪队列里的：从队列摘除，之后不会再被执行；
    /// - 定时器：从集合里摘除，循环也不必再为它等待；
    /// - 挂起在 epoll 上的：注销 fd，再把结果置为 Cancelled 并重新
    ///   入队，让协程以错误的形式恢复、有机会协作收尾；
    /// - 正等子任务的父协程：它自己没有挂起记录（记录属于子任务），这里只
    ///   能沿 waiting_on 去取消那个子任务，唤醒由子任务的收尾负责；
    void cancel(Handle &handle) {
        handle.cancelled = true;

        switch (handle.slot_kind) {
            using enum Kind;
        case kReady:
            // 排队中：不再执行。它的"结果"由下面的 on_cancel() 交代
            ready_.erase(handle.slot.ready);
            break;
        case kTimer:
            scheduled_.erase(handle.slot.timer);
            break;
        case kEvent: {
            auto *awaiter = handle.slot.awaiter;
            clear(handle);
            awaiter->on_cancel(); // 注销 fd，并把结果置为 Cancelled
            // 直接入队（绕开 cancelled 守卫），让它以错误的形式恢复
            enqueue_ready(handle);
            return;
        }
        case kNone:
            break;
        }
        clear(handle);

        // 下行：先摘掉这条边，再递归 —— 子任务收尾时会把它清掉（已经是
        // nullptr 就不重复清）并强制唤醒本协程，让它去读取消结果。
        if (auto *child = std::exchange(handle.waiting_on, nullptr)) {
            cancel(*child);
            return;
        }

        // 没有单条等待边：让钩子把它等的那一批也取消掉
        if (auto *hook = std::exchange(handle.cancel_hook, nullptr)) {
            hook->cancel_downstream();
        }

        handle.on_cancel();
    }

    /// 撤销该 handle 的一切挂起记录，但**不**重新入队。
    ///
    /// 与 cancel() 的区别：cancel() 会把挂起中的协程唤醒（重新入队，让
    /// co_await 拿到 Error::Cancelled）；而当持有者即将销毁协程帧时，那次
    /// 唤醒会指向已释放的对象，所以这里只做撤销、不做唤醒。
    void detach(Handle &handle) noexcept {
        handle.cancelled = true;

        if (handle.slot_kind == Kind::kNone) {
            return;
        }

        switch (handle.slot_kind) {
        case Kind::kReady:
            ready_.erase(handle.slot.ready);
            break;
        case Kind::kTimer:
            scheduled_.erase(handle.slot.timer);
            break;
        case Kind::kEvent: {
            auto *awaiter = handle.slot.awaiter;
            clear(handle);
            (void)awaiter->reset();
            return;
        }
        case Kind::kNone:
            return;
        }

        clear(handle);
    }

    void run_until_complete() {
        while (!stopped()) {
            (void)run_once();
        }
    }

    [[nodiscard]] auto wait_event(const Event &event) {
        return EventAwaiter{*this, event};
    }

    static EventLoop &loop() {
        thread_local EventLoop loop_{};
        return loop_;
    }

  private:
    EventLoop() = default;

    /// 登记/改写挂起记录。从"无记录"进入时才计数 +1。
    void mark(Handle &handle, Kind kind) {
        if (handle.slot_kind == Kind::kNone) {
            ++pending_;
        }
        handle.slot_kind = kind;
    }

    /// 清掉挂起记录.
    void clear(Handle &handle) {
        if (handle.slot_kind != Kind::kNone) {
            handle.slot_kind = Kind::kNone;
            --pending_;
        }
    }

    /// 入队并登记记录。
    ///
    /// 不变量：ready_ 里有节点 ⟺ 该 handle 的槽是 kReady。
    /// 已经在队列里就直接返回 —— 重复入队会让 run() 被调两次。
    void enqueue_ready(Handle &handle) {
        if (handle.slot_kind == Kind::kReady) {
            return;
        }

        ready_.push_back({.handle = &handle, .id = handle.id});
        mark(handle, Kind::kReady);
        handle.slot.ready = std::prev(ready_.end());
    }

    /// 登记一次 epoll 等待：fd -> handle 供事件回填，槽里的 awaiter 供
    /// cancel() 撤销注册
    void remember_event(Handle &handle, int fd, EventAwaiter *awaiter) {
        fd_owner_.insert_or_assign(fd, &handle);
        mark(handle, Kind::kEvent);
        handle.slot.awaiter = awaiter;
    }

    /// 注销 fd 登记并清掉该 handle 的 kEvent 槽（幂等）。
    void forget_event(int fd) {
        auto it = fd_owner_.find(fd);
        if (it == fd_owner_.end()) {
            return;
        }

        if (it->second != nullptr && it->second->slot_kind == Kind::kEvent) {
            clear(*it->second);
        }
        fd_owner_.erase(it);
    }

    Expected<> run_once();

  private:
    Epoller epoller_{};

    std::list<HandleInfo> ready_; // FIFO，可 O(1) 摘除
    std::set<TimerEntry> scheduled_;
    std::unordered_map<int, Handle *> fd_owner_; // 就绪 fd -> handle，回填用

    /// 当前持有挂起记录的 handle 数。
    size_t pending_{0};
};

inline Expected<> EventLoop::run_once() {
    std::optional<ms> timeout;

    if (!ready_.empty()) {
        timeout.emplace(ms::zero());
    } else if (!scheduled_.empty()) {
        auto &[when, _] = *scheduled_.begin();
        auto diff = when - Clock::now();
        auto duration_ms = std::chrono::duration_cast<ms>(diff);
        timeout.emplace(std::max(duration_ms, ms::zero()));
    }

    if (auto exp =
            epoller_.poll(timeout.has_value() ? timeout.value().count() : -1)
                .and_then([this](auto &&fds) -> Expected<> {
                    for (int fd : fds) {
                        if (auto it = fd_owner_.find(fd);
                            it != fd_owner_.end() && it->second) {
                            auto *handle = it->second;
                            if (handle->slot_kind == Kind::kEvent) {
                                (void)handle->slot.awaiter->reset();
                            }

                            if (!handle->cancelled) {
                                enqueue_ready(*handle);
                            }
                        }
                    }
                    return {};
                });
        !exp) {
        return exp;
    }

    auto now = Clock::now();
    for (auto it = scheduled_.begin(); it != scheduled_.end();
         it = scheduled_.erase(it)) {
        auto &[when, info] = *it;
        if (when > now) {
            break;
        }
        if (info.handle) {
            // kTimer -> kReady
            enqueue_ready(*info.handle);
        }
    }

    while (!ready_.empty()) {
        auto info = std::move(ready_.front());
        ready_.pop_front();

        if (info.handle != nullptr && info.handle->slot_kind == Kind::kReady) {
            clear(*info.handle);
        }

        if (info.handle) {
            info.handle->run();
        }
    }

    return {};
}

inline Expected<> EventAwaiter::reset() noexcept {
    if (!std::exchange(registered_, false)) {
        return {};
    }

    loop_.forget_event(event_.fd);
    return loop_.epoller_.unregister_event(event_);
}

template <Promise P>
bool EventAwaiter::await_suspend(std::coroutine_handle<P> coro) {
    auto &promise = coro.promise();

    if (auto exp = registered_ ? loop_.epoller_.modify_event(event_)
                               : loop_.epoller_.register_event(event_)) {
        registered_ = true;
        exp_ = std::move(exp);
        loop_.remember_event(promise, event_.fd, this);
        return true;
    } else {
        exp_ = std::move(exp);
        return false;
    }
}

} // namespace netx::core::details
