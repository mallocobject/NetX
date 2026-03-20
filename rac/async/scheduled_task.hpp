#ifndef RAC_ASYNC_SCHEDULED_TASK_HPP
#define RAC_ASYNC_SCHEDULED_TASK_HPP

#include "rac/async/concepts.hpp"
#include <utility>
namespace rac
{
template <Future TaskT> struct ScheduledTask
{
	template <Future Fut>
	explicit ScheduledTask(Fut&& fut) noexcept : task_(std::forward<Fut>(fut))
	{
		if (task_.valid() && !task_.done())
		{
			task_.coro_.promise().schedule();
		}
	}

	ScheduledTask(ScheduledTask&& other) noexcept
		: task_(std::move(other.task_))
	{
		if (task_.valid() && !task_.done())
		{
			task_.coro_.promise().schedule();
		}
	}

	~ScheduledTask() = default;

	decltype(auto) result() &
	{
		return task_.result();
	}

	decltype(auto) result() &&
	{
		return std::move(task_).result();
	}

	auto operator co_await() const& noexcept
	{
		return task_.operator co_await();
	}

	auto operator co_await() && noexcept
	{
		return std::move(task_).operator co_await();
	}

	bool valid() const
	{
		return task_.valid();
	}
	bool done() const
	{
		return task_.done();
	}

	void cancel()
	{
		task_.destroy();
	}

  private:
	TaskT task_;
};

template <Future Fut> ScheduledTask(Fut&&) -> ScheduledTask<Fut>;

template <Future Fut>
[[nodiscard(
	"discard(detached) a task will not schedule to run")]] ScheduledTask<Fut>
co_spawn(Fut&& fut) // 内部无 await，需要 co_await 延长生命周期
{
	return ScheduledTask{std::forward<Fut>(fut)};
}
} // namespace rac

#endif