#ifndef NETX_ASYNC_HANDLE_HPP
#define NETX_ASYNC_HANDLE_HPP

#include <atomic>
#include <cstdint>
namespace netx
{
namespace async
{
using HandleId = std::uint64_t;

struct Handle
{
	enum class State : std::uint8_t
	{
		kUnScheduled,
		kSuspend,
		kScheduled,
		kCancelled,
	};

	Handle() noexcept : handle_id_(handle_id_generation_++)
	{
	}

	virtual ~Handle() = default;

	virtual void run() = 0;

	void setState(State state)
	{
		state_ = state;
	}

	State state() const
	{
		return state_;
	}

	HandleId handleId()
	{
		return handle_id_;
	}

  private:
	HandleId handle_id_;
	// inline static std::atomic<HandleId> handle_id_generation_{0};
	// inline static HandleId handle_id_generation_{0};
	inline static thread_local HandleId handle_id_generation_{0};

  protected:
	State state_{Handle::State::kUnScheduled};
};
} // namespace async
} // namespace netx

#endif