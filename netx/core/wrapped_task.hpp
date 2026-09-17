#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/coro_handle.hpp"
#include "netx/core/task.hpp"
#include <list>
#include <type_traits>
#include <utility>

namespace netx {
namespace core {
namespace details {
/// 约定：这里的任务**没有父级协程** —— 没人 co_await 它。
template <Future TaskT>
class WrappedTask {
  private:
    using TaskList = std::list<WrappedTask>;
    using TaskListIter = TaskList::iterator;

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

  public:
    /// 空壳：task_ 为 nullptr，valid() 为假。
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

    // decltype(auto) result() & {
    //     return task_.operator co_await().await_resume();
    // }

    // decltype(auto) result() && {
    //     return std::move(task_).operator co_await().await_resume();
    // }

    template <typename Self>
    decltype(auto) result(this Self &&self) {
        if constexpr (std::is_lvalue_reference_v<Self>) {
            return (self.task_.operator co_await().await_resume());
        } else {
            return std::move(self.task_).operator co_await().await_resume();
        }
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
    void cancel() {
        task_.destroy();
    }

  private:
    TaskT task_{nullptr};
};
} // namespace details

static_assert(std::is_default_constructible_v<details::WrappedTask<Task<>>>);

template <details::Future Fut>
[[nodiscard("discard(detached) a task will not be scheduled to run")]]
details::WrappedTask<Fut> co_spawn(Fut &&fut) {
    return details::WrappedTask<Fut>{std::move(fut)};
}
} // namespace core
} // namespace netx