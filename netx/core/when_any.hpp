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
#include <type_traits>
#include <utility>
#include <variant>
namespace netx
{
namespace core
{
namespace details
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
		if constexpr (std::is_same_v<T, NonVoidHelper<void>>)
		{
			co_await std::forward<A>(t);
			result = T{};
		}
		else
		{
			result = co_await std::forward<A>(t);
		}
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
	bool await_suspend(std::coroutine_handle<P> coro) const noexcept
	{
		bool has_no_done_task = false;
		auto& promise = coro.promise();
		for (const auto& t : ctl.tasks)
		{
			if (t.valid() && !t.done())
			{
				has_no_done_task = true;
				t.coro.promise().schedule();
			}
		}

		if (has_no_done_task)
		{
			promise.state = Handle::State::kSuspend;
			ctl.waiter = &promise;
		}

		return has_no_done_task;
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
Task<Expected<std::variant<NonVoidRetType<Ts>...>>> when_any_impl(
	std::index_sequence<Is...>, Ts&&... ts)
{
	WhenAnyCtlBlock ctl{};
	std::tuple<NonVoidRetType<Ts>...> results;
	Task<Expected<>> helpers[]{
		WhenAnyHelper(std::forward<Ts>(ts), ctl, std::get<Is>(results), Is)...};
	ctl.tasks = helpers;

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

	using VariantType = std::variant<NonVoidRetType<Ts>...>;
	VariantType out;

	((ctl.winner == Is
		  ? (
				[&]
				{
					if constexpr (std::is_void_v<
									  typename AwaitableTrait<Ts>::RetType>)
					{
						out.template emplace<Is>(NonVoidHelper<>{});
					}
					else
					{
						out.template emplace<Is>(
							std::move(std::get<Is>(results)));
					}
				}(),
				0)
		  : 0),
	 ...);

	co_return out;
}
} // namespace details

template <typename... Ts> auto when_any(Ts&&... ts)
{
	return details::when_any_impl(std::make_index_sequence<sizeof...(Ts)>{},
								  std::forward<Ts>(ts)...);
}
} // namespace core
} // namespace netx