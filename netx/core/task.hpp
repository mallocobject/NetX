#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/coro_handle.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/handle.hpp"
#include "netx/core/result.hpp"
#include <coroutine>
#include <type_traits>
#include <utility>
namespace netx
{
namespace core
{
template <typename T = void> struct [[nodiscard]] Task
{
	struct promise_type : CoroHandle, Result<T>
	{
		promise_type()
		{
		}

		Task get_return_object() noexcept
		{
			return Task{
				std::coroutine_handle<promise_type>::from_promise(*this)};
		}

		std::suspend_always initial_suspend() const noexcept
		{
			return {};
		}

		std::suspend_always final_suspend() noexcept
		{
			if (continuation)
			{
				continuation->schedule();
			}
			return {};
		}

		template <typename U> auto await_transform(Expected<U>&& exp)
		{
			struct
			{
				bool await_ready() const noexcept
				{
					return exp.has_value();
				}

				void await_suspend(
					std::coroutine_handle<promise_type> coro) noexcept
				{
					auto& promise = coro.promise();
					promise.put_value(std::move(exp.error()));
					// promise.cancel();
					if (promise.continuation)
					{
						promise.continuation->schedule();
					}
				}

				U await_resume()
				{
					return std::move(exp.value());
				}

				Expected<U> exp;
			} awaiter{std::move(exp)};
			return awaiter;
		}

		template <typename U> auto await_transform(Expected<U>& exp)
		{
			return await_transform(std::move(exp));
		}

		template <typename U> auto await_transform(Task<U>& task)
		{
			return task.operator co_await();
		}

		template <typename U> auto await_transform(Task<U>&& task)
		{
			return std::move(task).operator co_await();
		}

		template <typename A> A&& await_transform(A&& awaiter) noexcept
		{
			return std::forward<A>(awaiter);
		}

		void run() override final
		{
			std::coroutine_handle<promise_type>::from_promise(*this).resume();
		}

		CoroHandle* continuation{nullptr};
	};

	struct AwaiterBase
	{
		bool await_ready() const noexcept
		{
			return callee_coro ? callee_coro.done() : true;
		}

		template <typename P>
		void await_suspend(std::coroutine_handle<P> caller_coro) const noexcept
		{
			static_assert(std::is_base_of_v<CoroHandle, P>,
						  "Caller promise must inherit CoroHandle");

			auto* caller = &caller_coro.promise();

			caller->state = Handle::State::kSuspend;
			callee_coro.promise().continuation = caller;
			callee_coro.promise().schedule();
		}

		std::coroutine_handle<promise_type> callee_coro;
	};

	explicit Task(std::coroutine_handle<promise_type> coro) : coro(coro)
	{
	}

	~Task()
	{
		destroy();
	}

	Task(Task&& other) : coro(std::exchange(other.coro, nullptr))
	{
	}

	Task& operator=(Task&& other)
	{
		if (this == &other)
		{
			return *this;
		}

		destroy();
		std::swap(coro, other.coro);
		return *this;
	}

	auto operator co_await() const& noexcept;
	auto operator co_await() && noexcept;

	constexpr bool valid() const noexcept
	{
		return coro != nullptr;
	}

	constexpr bool done() const noexcept
	{
		return coro.done();
	}

  private:
	void destroy()
	{
		if (auto handle = std::exchange(coro, nullptr))
		{
			handle.promise().cancel();
			handle.destroy();
		}
	}

  public:
	std::coroutine_handle<promise_type> coro;
};

template <typename T> auto Task<T>::operator co_await() const& noexcept
{
	struct : AwaiterBase
	{
		decltype(auto) await_resume() const
		{
			if constexpr (std::is_void_v<T>)
			{
				AwaiterBase::callee_coro.promise().result();
				return;
			}
			else
			{
				return AwaiterBase::callee_coro.promise().result();
			}
		}
	} awaiter{coro};

	return awaiter;
}

template <typename T> auto Task<T>::operator co_await() && noexcept
{
	struct : AwaiterBase
	{
		decltype(auto) await_resume() const
		{
			if constexpr (std::is_void_v<T>)
			{
				std::move(AwaiterBase::callee_coro.promise()).result();
				return;
			}
			else
			{
				return std::move(AwaiterBase::callee_coro.promise()).result();
			}
		}
	} awaiter{coro};

	return awaiter;
}
static_assert(Promise<Task<>::promise_type>);
static_assert(Future<Task<>>);
} // namespace core
} // namespace netx