#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/epoll_poller.hpp"
#include "netx/core/event.hpp"
#include "netx/core/handle.hpp"
#include <algorithm>
#include <chrono>
#include <coroutine>
#include <optional>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
namespace netx
{
namespace core
{
struct EventLoop
{
	struct EventAwaiter
	{
		bool await_ready() const noexcept
		{
			return false;
		}

		template <typename P> void await_suspend(std::coroutine_handle<P> coro)
		{
			const auto& promise = coro.promise();
			promise.state = Handle::State::kSuspend;
			event.info = {promise.id, &promise};

			if (!std::exchange(registered, true))
			{
				poller.register_event(event);
			}
			else
			{
				poller.modify_event(event);
			}
		}

		void await_resume() noexcept
		{
			event.info = {};
		}

		void reset() noexcept
		{
			if (std::exchange(registered, false))
			{
				poller.unregister_event(event);
			}
		}

		~EventAwaiter()
		{
			reset();
		}

		EpollPoller& poller;
		Event event{};
		bool registered{false};
	};

	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;
	using Duration = Clock::duration;
	using ms = std::chrono::milliseconds;

	void call_soon(Handle& handle)
	{
		if (handle.state == Handle::State::kCancelled)
		{
			return;
		}

		handle.state = Handle::State::kScheduled;
		ready_.emplace(handle.id, &handle);
	}

	void call_at(TimePoint tp, Handle& handle)
	{
		if (handle.state == Handle::State::kCancelled)
		{
			return;
		}

		handle.state = Handle::State::kScheduled;
		TimerEntry te{tp, {handle.id, &handle}};
		scheduled_query_.try_emplace(handle.id, te);
		scheduled_.insert(std::move(te));
	}

	template <typename Rep, typename Period>
	void call_after(std::chrono::duration<Rep, Period> duration, Handle& handle)
	{
		call_at(Clock::now() + duration, handle);
	}

	void cancel(Handle& handle)
	{
		if (handle.state == Handle::State::kScheduled)
		{
			const auto& id = handle.id;
			if (auto it = scheduled_query_.find(id);
				it != scheduled_query_.end())
			{
				scheduled_.erase(it->second);
				scheduled_query_.erase(it);
			}
			else
			{
				cancelled_.insert(id);
			}
		}

		handle.state = Handle::State::kCancelled;
	}

	void run_until_complete()
	{
		while (!stopped())
		{
			run_once();
		}
	}

	[[nodiscard]] auto wait_event(const Event& event)
	{
		return EventAwaiter{poller, event};
	}

	static EventLoop& loop()
	{
		thread_local EventLoop loop_{};
		return loop_;
	}

  private:
	EventLoop() = default;
	constexpr bool stopped() const noexcept
	{
		return poller.stopped() && ready_.empty() && scheduled_.empty();
	}

	void run_once();

  private:
	EpollPoller poller{};

	using TimerEntry = std::pair<TimePoint, HandleInfo>;

	std::queue<HandleInfo> ready_;
	std::set<TimerEntry> scheduled_;
	std::unordered_map<HandleId, TimerEntry> scheduled_query_;
	std::unordered_set<HandleId> cancelled_;
};
inline void EventLoop::run_once()
{
	std::optional<ms> timeout;

	if (!ready_.empty())
	{
		timeout.emplace(ms::zero());
	}
	else if (!scheduled_.empty())
	{
		auto& [when, _] = *scheduled_.begin();
		auto diff = when - Clock::now();
		auto duration_ms = std::chrono::duration_cast<ms>(diff);
		timeout.emplace(std::max(duration_ms, ms::zero()));
	}

	auto events =
		poller.poll(timeout.has_value() ? timeout.value().count() : -1);
	for (auto& event : events)
	{
		ready_.push(std::move(event.info));
	}

	auto now = Clock::now();
	while (!scheduled_.empty())
	{
		auto& [when, info] = *scheduled_.begin();
		if (when > now)
		{
			break;
		}
		ready_.push(std::move(info));
		scheduled_query_.erase(info.id);
		scheduled_.erase(scheduled_.begin());
	}

	while (!ready_.empty())
	{
		auto info = std::move(ready_.front());
		ready_.pop();
		if (auto it = cancelled_.find(info.id); it != cancelled_.end())
		{
			cancelled_.erase(it);
			continue;
		}

		info.handle->state = Handle::State::kUnscheduled;
		info.handle->run();
	}
}

static_assert(Awaiter<EventLoop::EventAwaiter>);
} // namespace core
} // namespace netx