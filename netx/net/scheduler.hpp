#ifndef NETX_NET_SCHEDULER_HPP
#define NETX_NET_SCHEDULER_HPP

#include "elog/logger.hpp"
#include "netx/async/check_error.hpp"
#include "netx/async/event_loop.hpp"
#include "netx/async/scheduled_task.hpp"
#include "netx/async/task.hpp"
#include "netx/meta/lock_free_queue.hpp"
#include <cstddef>
#include <latch>
#include <list>
#include <sys/eventfd.h>
#include <utility>
namespace netx
{
namespace net
{
namespace async = netx::async;
namespace meta = netx::meta;
class Scheduler
{
  public:
	Scheduler() = default;
	Scheduler(Scheduler&&) = delete;
	~Scheduler() = default;

	std::size_t size() const noexcept
	{
		return sts_.size();
	}

	void push(async::Task<>&& task)
	{
		task_queue_.push(std::move(task));
	}

	async::Task<> schedulerLoop(std::latch& start_latch)
	{
		start_latch.count_down();
		while (true)
		{
			co_await wakeup_awaiter_;
			shallow();
			// elog::LOG_TRACE("scheduler receives tasks, its address is {}",
			// 				static_cast<void*>(this));
			async::Task<> tmp(nullptr);

			while (task_queue_.pop(tmp))
			{
				sts_.emplace_back();
				auto it = std::prev(sts_.end());

				*it = async::ScheduledTask<async::Task<>>(std::move(tmp), &sts_,
														  it);
			}
		}
	}

	void wakeup()
	{
		uint64_t signal = 1;
		ssize_t n = ::write(wakeup_fd, &signal, sizeof(signal));

		if (n != sizeof(signal))
		{
			elog::LOG_ERROR("writes {} bytes instead of 8", n);
		}
	}

  private:
	void shallow()
	{
		uint64_t signal = 1;
		ssize_t n = ::read(wakeup_fd, &signal, sizeof(signal));

		if (n != sizeof(signal))
		{
			elog::LOG_ERROR("reads {} bytes instead of 8", n);
		}
	}

  private:
	meta::LockFreeQueue<async::Task<>> task_queue_;
	std::list<async::ScheduledTask<async::Task<>>> sts_;

	int wakeup_fd{async::checkError(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))};
	async::Event wakeup_ev_{.fd = wakeup_fd, .flags = async::Event::kEventRead};
	async::EventLoop::EventAwaiter wakeup_awaiter_{
		async::EventLoop::loop().wait_event(wakeup_ev_)};
};
} // namespace net
} // namespace netx

#endif