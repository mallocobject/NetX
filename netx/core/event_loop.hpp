#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/epoller.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/handle.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <list>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

namespace netx::core::details {
class EventLoop;

/// 等待一个 fd 上的事件。
///
/// 放在命名空间作用域而不是 EventLoop 内部：Handle 的挂起记录槽要能直接存
/// 它的指针，而嵌套类在 handle.hpp 里没法前置声明。
///
/// 成员函数里要碰 EventLoop 的私有状态（epoller_、remember_event、
/// forget_event），所以 EventLoop 把它声明成友元；定义则一律放到 EventLoop
/// 完整之后。
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

    EventAwaiter(EventAwaiter &&other) noexcept;
    EventAwaiter &operator=(EventAwaiter &&other) noexcept;

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
    /// 重新入队由 EventLoop 负责 —— 那边手里才有 Handle&。
    void on_cancel() noexcept {
        (void)reset();
        exp_ = std::unexpected(make_error_code(Error::Cancelled));
    }
};

class EventLoop {
  public:
    /// 沿用 EventLoop::EventAwaiter 这个写法
    using EventAwaiter = netx::core::details::EventAwaiter;

    friend class netx::core::details::EventAwaiter;

    static_assert(Awaiter<EventAwaiter>);

  private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;
    using ms = std::chrono::milliseconds;

    using Kind = Handle::SlotKind;

  private:
    Epoller epoller_{};

    std::list<HandleInfo> ready_; // FIFO，可 O(1) 摘除
    std::set<TimerEntry> scheduled_;
    std::unordered_map<int, Handle *> fd_owner_; // 就绪 fd -> handle，回填用

    /// 当前持有挂起记录的 handle 数。挂起记录内联在 Handle 里之后，没有
    /// 一张表可以问"还有没有待办"了，就靠这个计数。
    size_t pending_{0};

  private:
    EventLoop() = default;

    /// 登记/改写挂起记录。从"无记录"进入时才计数 +1。
    void mark(Handle &handle, Kind kind) {
        if (handle.slot_kind == Kind::kNone) {
            ++pending_;
        }
        handle.slot_kind = kind;
    }

    /// 清掉挂起记录。本来就无记录时是空操作。
    void clear(Handle &handle) {
        if (handle.slot_kind != Kind::kNone) {
            handle.slot_kind = Kind::kNone;
            --pending_;
        }
    }

    /// 入队并登记记录。
    ///
    /// 不变量：ready_ 里有节点 ⟺ 该 handle 的槽是 kReady。
    /// 已经在队列里就直接返回 —— 重复入队会让 run() 被调两次，而在一个
    /// 已完成的协程上再 resume 是未定义行为。
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
    /// 按 fd 而不是按 id：EventAwaiter 的析构路径上只有 fd。
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

  public:
    /// 还有没有待办。挂起记录内联在各自的 Handle 里，pending_ 是它们的
    /// 计数：mark() 从 kNone 进入时 +1，clear() 归零时 -1。
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

    /// 强制入队，绕开上面那条守卫。
    ///
    /// 取消路径必须能把等待者叫回来：被取消的一方要醒一次，才能读到
    /// Cancelled 结果、并把取消传给自己的等待者（见 CoroHandle::wake）。
    /// 用 call_soon 会被守卫挡下，链条就断在这里。
    void wake(Handle &handle) {
        enqueue_ready(handle);
    }

    /// 在指定时刻执行。幂等规则同 call_soon，因此同一个 handle 不会同时存在
    /// 多条挂起记录。
    void call_at(TimePoint tp, Handle &handle) {
        if (handle.cancelled || handle.slot_kind != Kind::kNone) {
            return;
        }

        auto [it, inserted] =
            scheduled_.insert({tp, {.handle = &handle, .id = handle.id}});
        (void)inserted;
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
    /// - 挂起在 epoll 上的：注销 fd（内核侧的账必须现在销，否则 stopped()
    ///   永远为假、循环会死锁在 poll(-1)），再把结果置为 Cancelled 并重新
    ///   入队，让协程以错误的形式恢复、有机会协作收尾；
    /// - 正等子任务的父协程：它自己没有挂起记录（记录属于子任务），这里只
    ///   能沿 waiting_on 去取消那个子任务，唤醒由子任务的收尾负责；
    /// - 以上都没有：由 on_cancel() 给它一个取消结果并叫醒它的等待者 ——
    ///   否则上游就永久挂在那里了。
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
            clear(handle); // 先销账，下面的入队要建的是新记录
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
            clear(handle); // 先销账，reset() 内部还会再走一次 forget_event
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
                            // kEvent -> kReady：fd 就绪了。先在**这里**把内核侧
                            // 的账销掉（而不是等协程恢复时由 ~EventAwaiter
                            // 去销），否则排队中的 handle 还挂在 epoll 上，
                            // 之后把它取消也摘不掉那条注册，fd 再就绪一次
                            // 就把它唤醒了。
                            if (handle->slot_kind == Kind::kEvent) {
                                (void)handle->slot.awaiter->reset();
                            }
                            // 已取消的不再被事件唤醒（它欠的是结果，不是继续跑）
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
            // 这个 handle 的槽会从 kTimer 改写成 kReady；定时器条目本身则由
            // 循环的第三子句 erase 掉
            enqueue_ready(*info.handle);
        }
    }

    while (!ready_.empty()) {
        auto info = std::move(ready_.front());
        ready_.pop_front();

        // 先销记录：run() 里若重新入队，那是全新的一轮，不该被这次销账带走。
        // 只销 kReady —— 按不变量此刻它就该是 kReady，加这道判断是为了让
        // 计数不会因为任何意外的不一致而漂移。
        if (info.handle != nullptr && info.handle->slot_kind == Kind::kReady) {
            clear(*info.handle);
        }

        if (info.handle) {
            info.handle->run();
        }
    }

    return {};
}

// --- EventAwaiter 的定义：必须等 EventLoop 完整，才能碰它的私有成员 ---

inline EventAwaiter::EventAwaiter(EventAwaiter &&other) noexcept
    : loop_(other.loop_), event_(other.event_),
      registered_(std::exchange(other.registered_, false)),
      exp_(std::move(other.exp_)) {
}

inline EventAwaiter &EventAwaiter::operator=(EventAwaiter &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    (void)reset();
    event_ = other.event_;
    registered_ = std::exchange(other.registered_, false);
    exp_ = std::move(other.exp_);
    return *this;
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
