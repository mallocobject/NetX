#ifndef NETX_ASYNC_WHEN_ANY_HPP
#define NETX_ASYNC_WHEN_ANY_HPP

#include "netx/async/awaitable_traits.hpp"
#include "netx/async/concepts.hpp"
#include "netx/async/result.hpp"
#include "netx/async/task.hpp"
#include <coroutine>
#include <cstddef>
#include <exception>
#include <span>
namespace netx
{
namespace async
{
struct WhenAnyCtrlBlock
{
	static constexpr std::size_t npos = static_cast<std::size_t>(-1);

	std::size_t winner{npos};
	CoroHandle* waiter{nullptr};
	std::exception_ptr exception{nullptr};
	std::span<const Task<>> tasks;

	bool try_complete(std::size_t index, std::exception_ptr ep)
	{
		// when winner has changed
		if (winner != npos)
		{
			return false;
		}
		winner = index;
		exception = ep;

		for (int i = 0; i < tasks.size(); i++)
		{
			if (i != index)
			{
				tasks[i].coro_.promise().cancel();
			}
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

struct WhenAnyAwaiter
{
	WhenAnyCtrlBlock& ctrl;
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
Task<> whenAnyHelper(A&& t, WhenAnyCtrlBlock& ctrl, Result<T>& result,
					 std::size_t index)
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
		ctrl.try_complete(index, nullptr);
	}
	catch (...)
	{
		ctrl.try_complete(index, std::current_exception());
	}
	co_return;
}

template <std::size_t... Is, typename... Ts>
Task<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>> whenAnyImpl(
	std::index_sequence<Is...>, Ts&&... ts)
{
	WhenAnyCtrlBlock ctrl{};

	std::tuple<Result<typename AwaitableTraits<Ts>::RetType>...> results;
	Task<> helpers[]{whenAnyHelper(ts, ctrl, std::get<Is>(results), Is)...};
	ctrl.tasks = helpers;

	co_await WhenAnyAwaiter{ctrl};
	std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...> out;
	((ctrl.winner == Is
		  ? (out.template emplace<Is>(std::get<Is>(results).result()), 0)
		  : 0),
	 ...);

	co_return out;
}

template <Awaitable... Ts>
	requires(sizeof...(Ts) != 0)
auto when_any(Ts&&... ts)
{
	return whenAnyImpl(std::make_index_sequence<sizeof...(Ts)>{},
					   std::forward<Ts>(ts)...);
}
} // namespace async
} // namespace netx

#endif