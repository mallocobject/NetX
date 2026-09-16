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
class EventLoop {
  public:
    class EventAwaiter {
      private:
        EventLoop &loop_;
        Event event_{};
        HandleId handle_id_{0};
        bool registered_{false};
        Expected<> exp_;

      public:
        explicit EventAwaiter(EventLoop &loop, const Event &event)
            : loop_(loop), event_(event) {
        }

        /// 注销 fd 并清掉循环里的 kEvent 记录；可重复调用
        Expected<> reset() noexcept {
            if (!std::exchange(registered_, false)) {
                return {};
            }

            loop_.forget_event(handle_id_, event_.fd);
            return loop_.epoller_.unregister_event(event_);
        }

        ~EventAwaiter() {
            (void)reset();
        }

        EventAwaiter(EventAwaiter &&other) noexcept
            : loop_(other.loop_), event_(other.event_),
              handle_id_(other.handle_id_),
              registered_(std::exchange(other.registered_, false)),
              exp_(std::move(other.exp_)) {
        }

        EventAwaiter &operator=(EventAwaiter &&other) noexcept {
            if (this == &other) {
                return *this;
            }

            (void)reset();
            event_ = other.event_;
            handle_id_ = other.handle_id_;
            registered_ = std::exchange(other.registered_, false);
            exp_ = std::move(other.exp_);
            return *this;
        }

        bool await_ready() const noexcept {
            return false;
        }

        template <Promise P>
        bool await_suspend(std::coroutine_handle<P> coro) {
            auto &promise = coro.promise();
            handle_id_ = promise.id;

            if (auto exp = registered_
                               ? loop_.epoller_.modify_event(event_)
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

        Expected<> await_resume() noexcept {
            return std::move(exp_);
        }

        /// 由 EventLoop::cancel() 调用：注销 fd 并把结果置为 Cancelled。
        /// 重新入队由 EventLoop 负责 —— 那边手里才有 Handle&。
        void on_cancel() noexcept {
            (void)reset();
            exp_ = std::unexpected(make_error_code(Error::Cancelled));
        }
    };

    static_assert(Awaiter<EventAwaiter>);

  private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;
    using ms = std::chrono::milliseconds;

    using TimerEntry = std::pair<TimePoint, HandleInfo>;

    /// 一个 handle 的挂起记录。ready / timer / event 三种形态互斥，用一张表
    /// 统一管理，cancel() 才能做到完备且幂等。
    struct Slot {
        enum class Kind : std::uint8_t { kReady, kTimer, kEvent } kind;

        std::list<HandleInfo>::iterator ready{}; // kReady：O(1) 摘除
        std::set<TimerEntry>::iterator timer{};  // kTimer
        EventAwaiter *awaiter{nullptr}; // kEvent：撤销 epoll 注册的入口
    };

  private:
    Epoller epoller_{};

    std::list<HandleInfo> ready_; // FIFO，可 O(1) 摘除
    std::set<TimerEntry> scheduled_;
    std::unordered_map<HandleId, Slot> slots_; // 唯一事实来源
    std::unordered_map<int, Handle *> fd_owner_; // 就绪 fd -> handle，回填用

  private:
    EventLoop() = default;

    /// 入队并登记记录。同一 id 会被覆盖，所以调用方需要自行判重。
    void enqueue_ready(Handle &handle) {
        ready_.push_back({.handle = &handle, .id = handle.id});
        slots_.insert_or_assign(
            handle.id,
            Slot{.kind = Slot::Kind::kReady, .ready = std::prev(ready_.end())});
    }

    /// 登记一次 epoll 等待：fd -> handle 供事件回填，挂起记录里的 awaiter
    /// 供 cancel() 撤销注册
    void remember_event(Handle &handle, int fd, EventAwaiter *awaiter) {
        fd_owner_.insert_or_assign(fd, &handle);
        slots_.insert_or_assign(
            handle.id, Slot{.kind = Slot::Kind::kEvent, .awaiter = awaiter});
    }

    /// 注销 fd 登记并清掉 kEvent 记录（幂等）
    void forget_event(HandleId id, int fd) {
        fd_owner_.erase(fd);
        if (auto it = slots_.find(id);
            it != slots_.end() && it->second.kind == Slot::Kind::kEvent) {
            slots_.erase(it);
        }
    }

    Expected<> run_once();

  public:
    /// 还有没有待办。ready / timer / epoll 注册全都记在 slots_ 里，所以只看这
    /// 一张表就够 —— 循环的终止条件不再依赖任何计数器，也就不会再出现"漏减
    /// 一次计数就死在 poll(-1)"的情况。
    [[nodiscard]] bool stopped() const noexcept {
        return slots_.empty();
    }

    /// 该 handle 当前是否有挂起记录（就绪 / 定时 / 等待事件）
    [[nodiscard]] bool pending(const Handle &handle) const noexcept {
        return slots_.contains(handle.id);
    }

    /// 尽快执行一次。幂等：已取消或已有挂起记录的 handle 不会重复入队。
    void call_soon(Handle &handle) {
        if (handle.cancelled || slots_.contains(handle.id)) {
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
        if (handle.cancelled || slots_.contains(handle.id)) {
            return;
        }

        auto [it, inserted] =
            scheduled_.insert({tp, {.handle = &handle, .id = handle.id}});
        (void)inserted;
        slots_.insert_or_assign(handle.id,
                                Slot{.kind = Slot::Kind::kTimer, .timer = it});
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

        if (auto it = slots_.find(handle.id); it != slots_.end()) {
            switch (it->second.kind) {
                using enum Slot::Kind;
            case kReady:
                // 排队中：不再执行。它的"结果"由下面的 on_cancel() 交代
                ready_.erase(it->second.ready);
                break;
            case kTimer:
                scheduled_.erase(it->second.timer);
                break;
            case kEvent: {
                auto *awaiter = it->second.awaiter;
                slots_.erase(it); // 先销账，下面的入队会以同一 id 建新记录
                awaiter->on_cancel(); // 注销 fd，并把结果置为 Cancelled
                // 直接入队（绕开 cancelled 守卫），让它以错误的形式恢复
                enqueue_ready(handle);
                return;
            }
            }
            slots_.erase(it);
        }

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

        auto it = slots_.find(handle.id);
        if (it == slots_.end()) {
            return;
        }

        switch (it->second.kind) {
        case Slot::Kind::kReady:
            ready_.erase(it->second.ready);
            break;
        case Slot::Kind::kTimer:
            scheduled_.erase(it->second.timer);
            break;
        case Slot::Kind::kEvent: {
            auto *awaiter = it->second.awaiter;
            slots_.erase(it); // 先销账，reset() 内部还会再查一次
            (void)awaiter->reset();
            return;
        }
        }

        slots_.erase(it);
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
                            if (auto slot = slots_.find(handle->id);
                                slot != slots_.end() &&
                                slot->second.kind == Slot::Kind::kEvent) {
                                (void)slot->second.awaiter->reset();
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
            // 同一 id 的记录会从 kTimer 覆盖成 kReady，定时器条目本身则由
            // 循环的第三子句 erase 掉
            enqueue_ready(*info.handle);
        }
    }

    while (!ready_.empty()) {
        auto info = std::move(ready_.front());
        ready_.pop_front();

        // 先销记录：run() 里若重新入队，那是全新的一轮，不该被这次销账带走
        slots_.erase(info.id);

        if (info.handle) {
            info.handle->run();
        }
    }

    return {};
}
} // namespace netx::core::details