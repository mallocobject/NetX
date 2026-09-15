#pragma once

#include <compare>
#include <cstdint>

namespace netx::core::details {
using HandleId = std::uint64_t;
class Handle {
  private:
    inline static thread_local HandleId handle_id_generation_{0};

  public:
    const HandleId id{0};

    /// 是否已被要求停止，置位后不会自动清除（sticky）。调度状态现在由
    /// EventLoop 的挂起记录表派生，不再在这里维护；取消信号主要通过
    /// co_await 的结果（Error::Cancelled）传递给任务。
    bool cancelled{false};

    /// 我正在等待的对象。父协程 co_await 一个子任务时，这里指向子任务的
    /// promise。
    ///
    /// 父协程处于这种等待时**没有挂起记录** —— 槽属于被等待的那一方 ——
    /// 所以槽表里查不到它，只有这条边能把等待关系串起来，让
    /// EventLoop::cancel() 沿它向下把整条链取消掉。
    Handle *waiting_on{nullptr};

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

class HandleInfo {
  public:
    Handle *handle{nullptr};
    HandleId id{0};

  public:
    std::strong_ordering operator<=>(const HandleInfo &other) const noexcept {
        if (auto cmp = id <=> other.id; cmp != 0) {
            return cmp;
        }
        return handle <=> other.handle;
    }
};
} // namespace netx::core::details