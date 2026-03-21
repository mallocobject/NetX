#ifndef RAC_ASYNC_WHEN_ANY_HPP
#define RAC_ASYNC_WHEN_ANY_HPP

#include "rac/async/awaitable_traits.hpp"
#include "rac/async/concepts.hpp"
#include "rac/async/result.hpp"
#include "rac/async/task.hpp"
#include <coroutine>
#include <cstddef>
#include <exception>
#include <span>
namespace rac
{
struct WhenAnyCtrlBlock
{
	static constexpr std::size_t npos = static_cast<std::size_t>(-1);

	std::atomic_size_t winner{npos};
	CoroHandle* waiter{nullptr};
	std::exception_ptr exception{nullptr};
};

struct WhenAnyAwaiter
{
	WhenAnyCtrlBlock& ctrl;
	std::span<const Task<>> tasks;

	bool await_ready() const noexcept
	{
		return false;
	}

	template <Promise P>
	bool await_suspend(std::coroutine_handle<P> coro) const noexcept
	{
		ctrl.waiter = &coro.promise();
		for (auto const& t : tasks)
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
	auto try_win_and_wake = [&](std::exception_ptr ep)
	{
		std::size_t expected = WhenAnyCtrlBlock::npos;
		if (ctrl.winner.compare_exchange_strong(expected, index,
												std::memory_order_acq_rel))
		{
			ctrl.exception = ep; // ep == nullptr 表示正常完成
			if (ctrl.waiter)
			{
				ctrl.waiter->schedule();
			}
		}
	};
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
		try_win_and_wake(nullptr);
	}
	catch (...)
	{
		try_win_and_wake(std::current_exception());
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
	co_await WhenAnyAwaiter{ctrl, helpers};
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
} // namespace rac

#endif