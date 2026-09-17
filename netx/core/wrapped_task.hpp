#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/coro_handle.hpp"
#include <list>
#include <utility>

namespace netx {
namespace core {
namespace details {
/// 登记进"待办链表"里的任务包装。
///
/// 约定：这里的任务**没有父级协程** —— 没人 co_await 它。于是 promise 的
/// continuation_ 只可能是下面的摘节点句柄（owns 恒为 true），任务跑完时
/// 没有谁需要被唤醒，只把节点摘掉即可；任务中途被销毁时也由 promise 自己
/// 回收那个句柄。
///
/// 反过来，一个任务一旦被登记进来，就不该再被 co_await：AwaiterBase 会
/// 覆盖掉摘节点句柄，节点再也不会被摘除。读结果走 result()（它不登记
/// continuation）。
template <Future TaskT>
class WrappedTask {
  private:
    using TaskList = std::list<WrappedTask>;
    using TaskListIter = TaskList::iterator;

  private:
    /// 把本节点从所属链表里摘掉。摘除动作放在析构里，因为两条路径最终都会
    /// 走到这里：
    ///   - 任务正常跑完：final_suspend() 把它推给事件循环 → run() → delete
    ///     this；
    ///   - 任务在完成前被销毁：~promise_type() 负责 delete continuation_。
    /// 两处都只 delete 一次，所以节点也只会被摘一次。
    // 必须是 public 继承：class 的默认继承是 private，那样外部既不能把它
    // 转成 CoroHandle*（set_continuation 要用），也调不到 schedule()
    class DeleteNodeHandle : public CoroHandle {
      private:
        TaskList &owner;
        TaskListIter iter;

      public:
        DeleteNodeHandle(TaskList &o, TaskListIter it) noexcept
            : owner(o), iter(it) {
        }

        ~DeleteNodeHandle() {
            if (iter != owner.end()) {
                owner.erase(iter);
            }
        }

        void run() override final {
            delete this;
        }
    };

  private:
    TaskT task_{nullptr};

  public:
    /// 空壳：task_ 为 nullptr，valid() 为假。
    ///
    /// 调度链表需要它：登记一个任务得先用 emplace_back 把节点建出来、拿到
    /// 节点自身的迭代器，才能构造摘节点句柄，所以绕不开"先默认构造再移动
    /// 赋值"这一步。
    WrappedTask() = default;

    explicit WrappedTask(TaskT &&task) noexcept : task_(std::move(task)) {
        if (task_.valid() && !task_.done()) {
            task_.coro.promise().schedule();
        }
    }

    WrappedTask(TaskT &&task, TaskList &owner, TaskListIter iter) noexcept
        : task_(std::move(task)) {
        if (task_.valid()) {
            if (!task_.done()) {
                auto &promise = task_.coro.promise();
                // DeleteNodeHandle 是 new 出来的，交给 promise 托管：owns
                // 置位后，任务若在完成前被销毁，由 ~promise_type() 回收它。
                promise.set_continuation(new DeleteNodeHandle{owner, iter},
                                         true);
                promise.schedule();
            } else {
                // 登记时任务已经完成：没有可等的东西，把摘节点推到下一轮。
                // 不能在这里就地 owner.erase(iter) —— 调用方通常的写法是
                //     auto it = std::prev(list.end());   // 先入链才拿得到
                //     iterator *it = WrappedTask{task, list, it}; //
                //     再把这个临时对象搬进去
                // 此刻 iter 指向的那个节点正被本次移动赋值覆盖，就地 erase
                // 会让调用方手里的迭代器当场失效。
                auto *handle = new DeleteNodeHandle{owner, iter};
                handle->schedule();
            }
        }
    }

    WrappedTask(WrappedTask &&other) noexcept : task_(std::move(other.task_)) {
    }

    WrappedTask &operator=(WrappedTask &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        task_ = std::move(other.task_);
        return *this;
    }

    /// 读结果。**不登记 continuation**，所以是符合"没有父级协程"约定的唯一
    /// 读取方式：先 done() 确认已经跑完，再取。
    ///
    /// 这里刻意不提供 operator co_await()：AwaiterBase::await_suspend 会
    /// set_continuation(caller)，把摘节点句柄覆盖掉，节点从此不会再被摘除，
    /// 而那个 owns=true 的堆句柄也不会有人回收。
    ///
    /// 右值重载必须搬 task_ 本身：搬 awaiter 的话 task_ 仍是左值，会匹配到
    /// operator co_await() const&，那一路的 result() 返回的是帧里的 T&，而
    /// 帧在 ~WrappedTask 里就被释放了。
    decltype(auto) result() & {
        return task_.operator co_await().await_resume();
    }

    decltype(auto) result() && {
        return std::move(task_).operator co_await().await_resume();
    }

    constexpr bool valid() const noexcept {
        return task_.valid();
    }

    constexpr bool done() const noexcept {
        return task_.done();
    }

    /// 结果是否已经可取，口径同 Task::settled()。冻结在失败 co_await 处的
    /// 任务 done() 为假、settled() 为真，判断能否取值要用后者。
    [[nodiscard]] bool settled() const noexcept {
        return task_.settled();
    }

    /// 取消：注销调度并销毁协程帧。
    /// 注意任务被销毁时 promise 会回收 DeleteNodeHandle，节点随之被摘除，
    /// 也就是**本对象自己被销毁**。所以 task_.destroy() 必须是最后一句，
    /// 之后不得再访问任何成员。
    void cancel() {
        task_.destroy();
    }
};
} // namespace details

template <details::Future Fut>
[[nodiscard("discard(detached) a task will not be scheduled to run")]]
details::WrappedTask<Fut> co_spawn(Fut &&fut) {
    return details::WrappedTask<Fut>{std::move(fut)};
}
} // namespace core
} // namespace netx