#ifndef NETX_ASYNC_WHEN_ALL_HPP
#define NETX_ASYNC_WHEN_ALL_HPP

#include "netx/async/awaitable_traits.hpp"
#include "netx/async/coro_handle.hpp"
#include "netx/async/result.hpp"
#include "netx/async/task.hpp"
#include <cstddef>
#include <exception>
#include <span>
namespace netx
{
namespace async
{
struct WhenAllCtrlBlock
{
	std::size_t count;
	CoroHandle* waiter{nullptr};
	std::exception_ptr exception{nullptr};
	std::span<const Task<>> tasks;

	explicit WhenAllCtrlBlock(std::size_t count) : count(count)
	{
	}

	bool try_complete(std::exception_ptr ep)
	{
		count--;
		if (!ep && count)
		{
			return false;
		}

		// record the first exception
		if (ep && !exception)
		{
			exception = ep;
		}

		for (const auto& t : tasks)
		{
			t.coro_.promise().cancel();
		}

		auto* w = waiter;
		waiter = nullptr;
		if (w)
		{
			w->schedule();
		}
		return true;
	}
};

struct WhenAllAwaiter
{
	WhenAllCtrlBlock& ctrl;
	// std::span<const Task<>> tasks;

	bool await_ready() const noexcept
	{
		return false;
	}

	template <Promise P>
	bool await_suspend(std::coroutine_handle<P> coro) const noexcept
	{
		coro.promise().setState(Handle::State::kSuspend);
		ctrl.waiter = &coro.promise();
		for (auto const& t : ctrl.tasks)
		{
			t.coro_.promise().schedule();
		}
		return true;
	}

	void await_resume() const
	{
		if (ctrl.exception) [[unlikely]]
		{
			std::rethrow_exception(ctrl.exception);
		}
	}
};

template <Awaitable A, typename T>
Task<> whenAllHelper(A&& t, WhenAllCtrlBlock& ctrl, Result<T>& result)
{
	try
	{
		if constexpr (std::is_void_v<T>)
		{
			co_await std::forward<A>(t);
		}
		else
		{
			result.return_value(co_await std::forward<A>(t));
		}
		ctrl.try_complete(nullptr);
	}
	catch (...)
	{
		ctrl.try_complete(std::current_exception());
	}
	co_return;
}

template <std::size_t... Is, typename... Ts>
Task<std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>> whenAllImpl(
	std::index_sequence<Is...>, Ts&&... ts)
{
	WhenAllCtrlBlock ctrl{sizeof...(Ts)};

	std::tuple<Result<typename AwaitableTraits<Ts>::RetType>...> results;
	Task<> helpers[]{whenAllHelper(ts, ctrl, std::get<Is>(results))...};
	ctrl.tasks = helpers;

	co_await WhenAllAwaiter{ctrl};
	co_return std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>{
		std::get<Is>(results).result()...};
}

template <Awaitable... Ts>
	requires(sizeof...(Ts) != 0)
auto when_all(Ts&&... ts)
{
	return whenAllImpl(std::make_index_sequence<sizeof...(Ts)>{},
					   std::forward<Ts>(ts)...);
}
} // namespace async
} // namespace netx

#endif