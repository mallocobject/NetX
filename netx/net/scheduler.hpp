#pragma once

#include "elog/logger.hpp"
#include "netx/core/error.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/scheduled_task.hpp"
#include "netx/core/task.hpp"
#include "netx/net/lock_free_queue.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <latch>
#include <list>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
namespace netx
{
namespace net
{
namespace details
{
struct Scheduler
{
	static core::Expected<Scheduler> create()
	{
		int wakeup_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
		if (wakeup_fd == -1)
		{
			return core::details::from_errno(errno);
		}

		return Scheduler(wakeup_fd);
	}

	size_t size() const noexcept
	{
		return sts_.size();
	}

	core::Expected<> wakeup();

	void push(core::Task<>&& task)
	{
		task_queue_.push(std::move(task));
	}

	core::Task<core::Expected<>> scheduler_loop(std::latch& start_latch);

	Scheduler(Scheduler&& other) noexcept
		: task_queue_(std::move(other.task_queue_)),
		  sts_(std::move(other.sts_)),
		  wakeup_fd_(std::exchange(other.wakeup_fd_, -1)),
		  wakeup_awaiter_(std::move(other.wakeup_awaiter_))
	{
	}

  private:
	Scheduler(int wakeup_fd)
		: wakeup_fd_(wakeup_fd),
		  wakeup_awaiter_(core::details::EventLoop::loop().wait_event(
			  {.fd = wakeup_fd_, .flags = core::details::Event::kEventRead}))
	{
	}

	core::Expected<> shallow();

  private:
	LockFreeQueue<core::Task<>> task_queue_;
	std::list<core::details::ScheduledTask<core::Task<>>> sts_;

	int wakeup_fd_{-1};
	core::details::EventLoop::EventAwaiter wakeup_awaiter_;
};

inline core::Expected<> Scheduler::wakeup()
{
	uint64_t signal = 1;
	while (true)
	{
		ssize_t n = ::write(wakeup_fd_, &signal, sizeof(uint64_t));
		if (n == sizeof(uint64_t))
		{
			return {};
		}
		if (n == -1)
		{
			if (errno == EINTR)
			{
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				return {};
			}
			return core::details::from_errno(errno);
		}
		return core::details::make_error_code(
			core::details::Error::InvalidOperation);
	}
}

inline core::Expected<> Scheduler::shallow()
{
	uint64_t val = 0;
	while (true)
	{
		ssize_t n = ::read(wakeup_fd_, &val, sizeof(uint64_t));

		if (n == sizeof(uint64_t))
		{
			return {};
		}
		if (n == -1)
		{
			if (errno == EINTR)
			{
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				return {};
			}
			return core::details::from_errno(errno);
		}
		return core::details::make_error_code(
			core::details::Error::InvalidOperation);
	}
}

inline core::Task<core::Expected<>> Scheduler::scheduler_loop(
	std::latch& start_latch)
{
	start_latch.count_down();

	while (true)
	{
		if (auto exp = co_await wakeup_awaiter_; !exp.has_value())
		{
			const std::error_code& ec = exp.error();
			elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
			co_return {};
		}
		if (auto exp = shallow(); !exp.has_value())
		{
			const std::error_code& ec = exp.error();
			elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
			co_return {};
		}

		core::Task<> tmp{nullptr};
		while (task_queue_.pop(tmp))
		{
			sts_.emplace_back();
			auto it = std::prev(sts_.end());

			*it = core::details::ScheduledTask<core::Task<>>(std::move(tmp),
															 sts_, it);
		}
	}
}
} // namespace details
} // namespace net
} // namespace netx