#pragma once

#include <compare>
#include <cstdint>
namespace netx
{
namespace core
{
using HandleId = std::uint64_t;
struct Handle
{
	Handle() : id(++handle_id_genaration_)
	{
	}

	virtual void run() = 0;
	virtual ~Handle() = default;

	enum class State : std::uint8_t
	{
		kUnscheduled,
		kSuspend,
		kScheduled,
		kCancelled,
	} state{Handle::State::kUnscheduled};

	const HandleId id{0};

  private:
	inline static thread_local HandleId handle_id_genaration_{0};
};

struct HandleInfo
{
	std::strong_ordering operator<=>(const HandleInfo& other) const noexcept
	{
		if (auto cmp = id <=> other.id; cmp != 0)
		{
			return cmp;
		}
		return handle <=> other.handle;
	}

	HandleId id{0};
	Handle* handle{nullptr};
};
} // namespace core
} // namespace netx