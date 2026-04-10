#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/error.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include <chrono>
#include <coroutine>
#include <unistd.h>
#include <utility>
namespace netx
{
namespace core
{
namespace details
{
template <typename Duration> struct SleepAwaiter
{
	explicit SleepAwaiter(Duration&& duration) : dora(std::move(duration))
	{
	}

	bool await_ready() const noexcept
	{
		return false;
	}

	template <Promise P>
	void await_suspend(std::coroutine_handle<P> coro) const noexcept
	{
		EventLoop::loop().call_after(dora, coro.promise());
	}

	void await_resume() const noexcept
	{
	}

	Duration dora{};
};
} // namespace details

template <typename Rep, typename Period>
Task<Expected<>> sleep(details::NoWaitAtInitialSuspend,
					   std::chrono::duration<Rep, Period>&& duration)
{
	co_await details::SleepAwaiter{std::move(duration)};
	co_return details::Error::Timeout;
}

template <typename Rep, typename Period>
Task<Expected<>> sleep(std::chrono::duration<Rep, Period> duration)
{
	co_return co_await sleep(details::no_wait_at_initial_suspend,
							 std::move(duration));
}
} // namespace core
} // namespace netx