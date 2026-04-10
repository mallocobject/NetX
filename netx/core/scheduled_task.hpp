#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/coro_handle.hpp"
#include <list>
#include <utility>
namespace netx
{
namespace core
{
namespace details
{
template <Future TaskT> struct ScheduledTask
{
	using TaskList = std::list<ScheduledTask>;
	using TaskListIter = TaskList::iterator;

	struct DeleteNodeHandle : CoroHandle
	{
		void run() override final
		{
			if (iter != owner.end())
			{
				owner.erase(iter);
			}
			delete this;
		}

		TaskList& owner;
		TaskListIter iter;
	};

	ScheduledTask() = default;
	explicit ScheduledTask(TaskT&& task) noexcept : task_(std::move(task))
	{
		if (task_.valid() && !task_.done())
		{
			task_.coro.promise().schedule();
		}
	}

	ScheduledTask(TaskT&& task, TaskList& owner, TaskListIter iter) noexcept
		: task_(std::move(task))
	{
		if (task_.valid())
		{
			if (!task_.done())
			{
				auto& promise = task_.coro.promise();
				promise.continuation = new DeleteNodeHandle{owner, iter};
				promise.schedule();
			}
			else
			{
				if (iter != owner.end())
				{
					owner.erase(iter);
				}
			}
		}
	}

	ScheduledTask(ScheduledTask&& other) noexcept
		: task_(std::move(other.task_))
	{
	}

	ScheduledTask& operator=(ScheduledTask&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		task_ = std::move(other.task_);
		return *this;
	}

	decltype(auto) result() &
	{
		return task_.operator co_await().await_resume();
	}

	decltype(auto) result() &&
	{
		return std::move(task_.operator co_await()).await_resume();
	}

	auto operator co_await() const& noexcept
	{
		return task_.operator co_await();
	}

	auto operator co_await() && noexcept
	{
		return std::move(task_).operator co_await();
	}

	constexpr bool valid() const noexcept
	{
		return task_.valid();
	}

	constexpr bool done() const noexcept
	{
		return task_.done();
	}

	void cancel()
	{
		task_.destroy();
	}

  private:
	TaskT task_{nullptr};
};
} // namespace details

template <details::Future Fut>
[[nodiscard("discard(detached) a task will not be scheduled to run")]]
details::ScheduledTask<Fut> co_spawn(Fut&& fut)
{
	return details::ScheduledTask<Fut>{std::move(fut)};
}
} // namespace core
} // namespace netx