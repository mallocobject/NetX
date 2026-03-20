#ifndef RAC_ASYNC_TASK_HPP
#define RAC_ASYNC_TASK_HPP

#include "rac/async/coro_handle.hpp"
#include "rac/async/event_loop.hpp"
#include "rac/async/handle.hpp"
#include "rac/async/result.hpp"
#include <cassert>
#include <coroutine>
#include <format>
#include <stdexcept>
#include <utility>
namespace rac
{
struct NoWaitAtInitialSuspend
{
};
inline constexpr NoWaitAtInitialSuspend no_wait_at_initial_suspend;

template <typename T = void> struct Task
{
	struct promise_type;
	using coro_handle = std::coroutine_handle<promise_type>;

	template <Future> friend struct ScheduledTask;

	explicit Task(coro_handle coro) noexcept : coro_(coro)
	{
	}

	Task(Task&& other) noexcept : coro_(std::exchange(other.coro_, nullptr))
	{
	}

	~Task()
	{
		destroy();
	}

	decltype(auto) result() &
	{
		assert(coro_);
		return coro_.promise().result();
	}

	decltype(auto) result() &&
	{
		assert(coro_);
		return std::move(coro_.promise()).result();
	}

	struct Awaiter
	{

		bool await_ready() const noexcept
		{
			if (callee_coro) [[likely]]
			{
				return callee_coro.done();
			}
			return true;
		}

		template <typename P>
		void await_suspend(std::coroutine_handle<P> caller_coro) const noexcept
		{
			assert(!callee_coro.promise().continuation);
			caller_coro.promise().setState(Handle::State::kSuspend);
			callee_coro.promise().continuation = &caller_coro.promise();

			callee_coro.promise().schedule();
		}

		T await_resume() const
		{
			if (!callee_coro) [[unlikely]]
			{
				throw std::runtime_error("future is invalid");
			}

			if constexpr (std::is_void_v<T>)
			{
				callee_coro.promise().result();
				return;
			}
			else
			{
				return callee_coro.promise().result();
			}
		}

		explicit Awaiter(std::coroutine_handle<promise_type> callee_coro)
			: callee_coro(callee_coro)
		{
		}

		coro_handle callee_coro;
	};

	struct OwnedAwaiter : public Awaiter
	{
		using Awaiter::Awaiter;

		~OwnedAwaiter()
		{
			assert(Awaiter::callee_coro);
			Awaiter::callee_coro.destroy();
		}
	};

	auto operator co_await() const& noexcept
	{
		return Awaiter{coro_};
	}

	auto operator co_await() && noexcept
	{
		return OwnedAwaiter{std::exchange(coro_, nullptr)};
	}

	bool valid() const
	{
		return coro_ != nullptr;
	}
	bool done() const
	{
		return coro_.done();
	}

	struct promise_type : public CoroHandle, public Result<T>
	{
		promise_type() = default;

		template <typename... Args>
		promise_type(NoWaitAtInitialSuspend, Args&&...) noexcept
			: wait_at_initial_suspend(false)
		{
		}

		template <typename Obj, typename... Args>
		promise_type(Obj&&, NoWaitAtInitialSuspend, Args&&...) noexcept
			: wait_at_initial_suspend(false)
		{
		}

		auto initial_suspend() const noexcept
		{
			struct InitialSuspendAwaiter
			{
				bool await_ready() const noexcept
				{
					return !wait_at_initial_suspend;
				}

				void await_suspend(std::coroutine_handle<>) const noexcept
				{
				}

				void await_resume() const noexcept
				{
				}

				bool wait_at_initial_suspend{true};
			};

			return InitialSuspendAwaiter{wait_at_initial_suspend};
		}

		// Templates cannot be declared inside of a local class
		struct FinalSuspendAwaiter
		{
			bool await_ready() const noexcept
			{
				return false;
			}

			template <typename P>
			void await_suspend(
				std::coroutine_handle<P> self_coro) const noexcept
			{
				if (auto cont = self_coro.promise().continuation)
				{
					EventLoop::loop().call_soon(*cont);
				}
			}

			void await_resume() const noexcept
			{
			}
		};

		auto final_suspend() const noexcept
		{
			return FinalSuspendAwaiter{};
		}

		Task get_return_object() noexcept
		{
			return Task{coro_handle::from_promise(*this)};
		}

		void run() override final
		{
			coro_handle::from_promise(*this).resume();
		}

		// const std::source_location& frameInfo() const override final
		// {
		// 	return frame_info;
		// }
		void dumpBackTrace(size_t depth = 0) const override final
		{
			std::cout << std::format("[{}] {}\n", depth, frameName());
			if (continuation)
			{
				continuation->dumpBackTrace(depth + 1);
			}
			else
			{
				std::cout << '\n';
			}
		}

		const bool wait_at_initial_suspend{true};
		CoroHandle* continuation{
			nullptr}; // promise_type derive from CoroHandle
					  // std::source_location frame_info{};
	};

  private:
	void destroy()
	{
		if (auto handle = std::exchange(coro_, nullptr))
		{
			handle.promise().cancel();
			handle.destroy();
		}
	}

  private:
	coro_handle coro_;
};

static_assert(Promise<Task<>::promise_type>);
static_assert(Future<Task<>>);
} // namespace rac

#endif