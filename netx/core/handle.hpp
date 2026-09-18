#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <list>
#include <set>
#include <utility>

namespace netx::core::details {
using HandleId = std::uint64_t;

class Handle;

/// 等待 fd 事件的对象.
class EventAwaiter;

/// 调度用的时钟。定时器集合按时刻排序，必须和 EventLoop 用同一个。
using ScheduleClock = std::chrono::steady_clock;

struct HandleInfo {
    inline std::strong_ordering
    operator<=>(const HandleInfo &other) const noexcept {
        if (auto cmp = id <=> other.id; cmp != 0) {
            return cmp;
        }
        return handle <=> other.handle;
    }

    Handle *handle{nullptr};
    HandleId id{0};
};

/// 定时器集合的元素：到期时刻 + 该 handle 的标识。
using TimerEntry = std::pair<ScheduleClock::time_point, HandleInfo>;

/// 一对多的等待关系（when_any）没法用单条 waiting_on 表达，
/// 挂个钩子：cancel() 沿 waiting_on 走不到的下游，由它兜底。
struct CancelHook {
    virtual void cancel_downstream() noexcept = 0;
};

class Handle {
  public:
    /// 挂起记录的形态。一个 handle 在任一时刻只处于其中一种，所以下面用
    /// 联合体存具体的引用。
    enum class SlotKind : std::uint8_t { kNone, kReady, kTimer, kEvent };

    /// 只在 slot_kind 指明的那一个成员有效。
    ///   kReady —— 在就绪队列里的位置，供 O(1) 摘除
    ///   kTimer —— 在定时器集合里的位置
    ///   kEvent —— 撤销 epoll 注册的入口
    union SlotRef {
        std::list<HandleInfo>::iterator ready;
        std::set<TimerEntry>::iterator timer;
        EventAwaiter *awaiter;
    };

    Handle() : id(++handle_id_generation_) {
    }

    virtual ~Handle() = default;

    virtual void run() = 0;

    /// 被取消时的收尾钩子，由 EventLoop::cancel() 调用。
    virtual void on_cancel() noexcept {
    }

    // 父协程 co_await 一个子任务时，这里指向子任务的 promise。
    Handle *waiting_on{nullptr};
    CancelHook *cancel_hook{nullptr};

    SlotKind slot_kind{SlotKind::kNone};
    SlotRef slot{};

    const HandleId id{0};

    /// 是否已被要求停止，置位后不会自动清除（sticky）。取消信号主要通过
    /// co_await 的结果（Error::Cancelled）传递给任务。
    bool cancelled{false};

  private:
    inline static thread_local HandleId handle_id_generation_{0};
};

} // namespace netx::core::details
