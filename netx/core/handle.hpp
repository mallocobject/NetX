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

/// 调度用的时钟。定时器集合按时刻排序，必须和 EventLoop 用同一个。
using ScheduleClock = std::chrono::steady_clock;

class HandleInfo {
  public:
    Handle *handle{nullptr};
    HandleId id{0};

  public:
    /// 定义在 Handle 之后：比较指针要求类型完整。
    std::strong_ordering operator<=>(const HandleInfo &other) const noexcept;
};

/// 定时器集合的元素：到期时刻 + 该 handle 的标识。
using TimerEntry = std::pair<ScheduleClock::time_point, HandleInfo>;

class Handle {
  private:
    inline static thread_local HandleId handle_id_generation_{0};

  public:
    /// 挂起记录的形态。一个 handle 在任一时刻只处于其中一种，所以下面用
    /// 联合体存具体的引用。
    ///
    /// 这些字段原先在 EventLoop::slots_ 里（一张 unordered_map<HandleId,
    /// Slot>），挂起/恢复各要插入、删除一条哈希记录，perf 里占到约 15%。
    /// 而绝大多数调用点手里本来就有 Handle&，把它内联进来这张表就不需要了。
    enum class SlotKind : std::uint8_t { kNone, kReady, kTimer, kEvent };

    /// 只在 slot_kind 指明的那一个成员有效。
    ///   kReady —— 在就绪队列里的位置，供 O(1) 摘除
    ///   kTimer —— 在定时器集合里的位置
    ///   kEvent —— fd，撤销注册时用它去 EventLoop 的 fd_owner_ 里查回
    ///            EventAwaiter（那是 EventLoop 的嵌套类，没法在这里前置
    ///            声明，所以槽里存 fd 而不是指针）
    union SlotRef {
        std::list<HandleInfo>::iterator ready;
        std::set<TimerEntry>::iterator timer;
        int fd;
    };

    const HandleId id{0};

    /// 是否已被要求停止，置位后不会自动清除（sticky）。取消信号主要通过
    /// co_await 的结果（Error::Cancelled）传递给任务。
    bool cancelled{false};

    /// 我正在等待的对象。父协程 co_await 一个子任务时，这里指向子任务的
    /// promise。
    ///
    /// 父协程处于这种等待时**没有挂起记录** —— 记录属于被等待的那一方 ——
    /// 所以只有这条边能把等待关系串起来，让 EventLoop::cancel() 沿它向下把
    /// 整条链取消掉。
    Handle *waiting_on{nullptr};

    SlotKind slot_kind{SlotKind::kNone};
    SlotRef slot{};

  public:
    Handle() : id(++handle_id_generation_) {
    }

    virtual ~Handle() = default;

    virtual void run() = 0;

    /// 被取消时的收尾钩子，由 EventLoop::cancel() 调用。
    ///
    /// 默认什么都不做：普通 Handle（比如 WrappedTask 的摘节点句柄）被取消
    /// 后只是不再执行，没有"结果"要交代。协程 promise 会覆盖它 —— 给尚未
    /// 定局的任务定一个取消结果并唤醒等待者，少了后半句，等它的一方就会
    /// 永久挂起。
    virtual void on_cancel() noexcept {
    }
};

inline std::strong_ordering
HandleInfo::operator<=>(const HandleInfo &other) const noexcept {
    if (auto cmp = id <=> other.id; cmp != 0) {
        return cmp;
    }
    return handle <=> other.handle;
}
} // namespace netx::core::details
