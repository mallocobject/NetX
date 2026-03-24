#ifndef RAC_ASYNC_EVENT_LOOP_HPP
#define RAC_ASYNC_EVENT_LOOP_HPP

#include "rac/async/epoll_poller.hpp"
#include "rac/async/event.hpp"
#include "rac/async/handle.hpp"
#include <algorithm>
#include <chrono>
#include <coroutine>
#include <optional>
#include <queue>
#include <set>
#include <unordered_set>
#include <utility>
namespace rac
{
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;
using ms = std::chrono::milliseconds;
struct EventLoop
{
	void call_soon(Handle& handle)
	{
		handle.setState(Handle::State::kScheduled);
		ready_.emplace(handle.handleId(), &handle);
	}

	void call_at(TimePoint time_point, Handle& handle)
	{
		handle.setState(Handle::State::kScheduled);
		schedule_.emplace(time_point, HandleInfo{handle.handleId(), &handle});
	}

	template <typename Rep, typename Period>
	void call_after(std::chrono::duration<Rep, Period> duration, Handle& handle)
	{
		call_at(Clock::now() + duration, handle);
	}

	void cancel_handle(Handle& handle)
	{
		handle.setState(Handle::State::kUnScheduled);
		cancelled_.insert(handle.handleId());
	}

	void run_until_complete()
	{
		while (!stopped())
		{
			runOnce();
		}
	}

	struct EventAwaiter
	{
		bool await_ready() const noexcept
		{
			return false;
		}

		template <typename P>
		void await_suspend(std::coroutine_handle<P> handle) noexcept
		{
			handle.promise().setState(Handle::State::kSuspend);
			event.handle_info = {handle.promise().handleId(),
								 &handle.promise()};

			if (!registered)
			{
				poller.registerEvent(event);
				registered = true;
			}
			else
			{
				poller.modifyEvent(event);
			}
		}

		void await_resume() noexcept
		{
			event.handle_info = {};
		}

		void reset() noexcept
		{
			if (registered)
			{
				poller.unregisterEvent(event);
				registered = false;
			}
		}

		EventAwaiter(EpollPoller& poller, Event event)
			: poller(poller), event(std::move(event))
		{
		}

		EventAwaiter(EventAwaiter&& other) noexcept
			: poller(other.poller), event(std::exchange(other.event, {})),
			  registered(std::exchange(other.registered, false))
		{
		}

		~EventAwaiter()
		{
			reset();
		}

		EpollPoller& poller;
		Event event{};
		bool registered{false};
	};

	[[nodiscard]] auto wait_event(const Event& event)
	{
		return EventAwaiter{poller, event};
	}

	static EventLoop& loop()
	{
		thread_local EventLoop loop;
		return loop;
	}

	int epfd() const noexcept
	{
		return poller.epfd();
	}

	EventLoop(EventLoop&&) = delete;
	~EventLoop() = default;

  private:
	EventLoop() = default;

	bool stopped() const
	{
		return poller.stopped() && ready_.empty() && schedule_.empty();
	}

	void cleanupDelayedCall();

	void runOnce();

  private:
	EpollPoller poller{};
	std::queue<HandleInfo> ready_;
	using TimerEntry = std::pair<TimePoint, HandleInfo>;
	std::set<TimerEntry> schedule_;
	std::unordered_set<HandleId> cancelled_;
};

inline void EventLoop::cleanupDelayedCall()
{
	while (!schedule_.empty())
	{
		auto&& [when, handle_info] = *schedule_.begin();
		if (auto it = cancelled_.find(handle_info.id); it != cancelled_.end())
		{
			schedule_.erase(schedule_.begin());
			cancelled_.erase(it);
		}
		else
		{
			break;
		}
	}
}

inline void EventLoop::runOnce()
{
	std::optional<ms> timeout;
	if (!ready_.empty())
	{
		timeout.emplace(ms::zero());
	}
	else if (!schedule_.empty())
	{
		auto& [when, _] = *schedule_.begin();
		auto diff = when - Clock::now();
		auto duration_ms = std::chrono::duration_cast<ms>(diff);
		timeout.emplace(std::max(duration_ms, ms::zero()));
	}

	auto events =
		poller.poll(timeout.has_value() ? timeout.value().count() : -1);
	for (auto& event : events)
	{
		ready_.push(std::move(event.handle_info));
	}

	auto now = Clock::now();
	while (!schedule_.empty())
	{
		auto& [when, info] = *schedule_.begin();
		if (when > now)
		{
			break;
		}
		ready_.push(std::move(info));
		schedule_.erase(schedule_.begin());
	}

	while (!ready_.empty())
	{
		auto info = std::move(ready_.front());
		ready_.pop();
		if (auto it = cancelled_.find(info.id); it != cancelled_.end())
		{
			cancelled_.erase(it);
		}
		else
		{
			info.handle->setState(
				Handle::State::kUnScheduled); // 先设置状态，再
											  // run，防止覆盖后续状态
			info.handle->run();
		}
	}

	cleanupDelayedCall();
}
} // namespace rac

#endif