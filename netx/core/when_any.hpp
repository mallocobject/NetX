#pragma once

#include "netx/core/awaitable_trait.hpp"
#include "netx/core/concepts.hpp"
#include "netx/core/coro_handle.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/handle.hpp"
#include "netx/core/task.hpp"
#include <coroutine>
#include <cstddef>
#include <exception>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
namespace netx
{
namespace core
{
struct WhenAnyCtlBlock
{
	static constexpr size_t npos{static_cast<size_t>(-1)};

	size_t winner{npos};
	CoroHandle* waiter{nullptr};
	std::exception_ptr exception;
	std::span<const Task<Expected<>>> tasks;

	bool try_complete(size_t index, std::exception_ptr ep)
	{
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
				tasks[i].coro.promise().cancel();
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

template <Awaitable A, typename T>
Task<Expected<>> WhenAnyHelper(A&& t, WhenAnyCtlBlock& ctl, Expected<T>& result,
							   size_t index)
{
	try
	{
		result = std::move(co_await std::forward<A>(t));
		ctl.try_complete(index, nullptr);
	}
	catch (...)
	{
		ctl.try_complete(index, std::current_exception());
	}
	co_return {};
}

struct WhenAnyAwaiter
{
	bool await_ready() const noexcept
	{
		return false;
	}

	template <typename P>
	void await_suspend(std::coroutine_handle<P> coro) const noexcept
	{
		auto& promise = coro.promise();
		promise.state = Handle::State::kSuspend;
		ctl.waiter = &promise;
		for (const auto& t : ctl.tasks)
		{
			t.coro.promise().schedule();
		}
	}

	void await_resume() const
	{
		if (ctl.exception) [[unlikely]]
		{
			std::rethrow_exception(ctl.exception);
		}
	}

	WhenAnyCtlBlock& ctl;
};

template <size_t... Is, typename... Ts>
Task<Expected<std::variant<UnpackedRetType<Ts>...>>> when_any_impl(
	std::index_sequence<Is...>, Ts&&... ts)
{
	WhenAnyCtlBlock ctl{};
	std::tuple<typename AwaitableTrait<Ts>::RetType...> results;
	Task<Expected<>> helper[]{
		WhenAnyHelper(std::forward<Ts>(ts), ctl, std::get<Is>(results), Is)...};
	ctl.tasks = helper;

	co_await WhenAnyAwaiter{ctl};

	std::error_code winner_ec;
	bool is_failed = false;

	((ctl.winner == Is
		  ? (!std::get<Is>(results).has_value()
				 ? (winner_ec = std::get<Is>(results).error(), is_failed = true)
				 : false)
		  : false),
	 ...);

	if (is_failed)
	{
		co_return winner_ec;
	}

	using VariantType = std::variant<UnpackedRetType<Ts>...>;
	VariantType out;

	((ctl.winner == Is
		  ? (
				[&]
				{
					if constexpr (std::is_void_v<
									  typename AwaitableTrait<Ts>::ValueType>)
					{
						out.template emplace<Is>(NonVoidHelper<>{});
					}
					else
					{
						out.template emplace<Is>(
							std::move(std::get<Is>(results)).value());
					}
				}(),
				0)
		  : 0),
	 ...);

	co_return out;
}

template <typename... Ts> auto when_any(Ts&&... ts)
{
	return when_any_impl(std::make_index_sequence<sizeof...(Ts)>{},
						 std::forward<Ts>(ts)...);
}
} // namespace core
} // namespace netx