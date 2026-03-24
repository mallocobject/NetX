#ifndef RAC_ASYNC_EVENT_HPP
#define RAC_ASYNC_EVENT_HPP

#include "rac/async/handle.hpp"
#include <compare>
#include <cstdint>
#include <sys/epoll.h>
namespace rac
{
struct HandleInfo
{
	HandleId id{};
	Handle* handle{nullptr};
	int bootstrap_fd{-1};

	std::strong_ordering operator<=>(const HandleInfo& other) const
	{
		if (auto cmp = id <=> other.id; cmp != 0)
		{
			return cmp;
		}
		return handle <=> other.handle;
	}

	bool operator==(const HandleInfo& other) const = default;
};

struct Event
{
	// enum class Flags : std::uint32_t
	// {
	//     kEventRead = EPOLLIN,
	//     kEventWrite = EPOLLOUT,
	// }
	static constexpr std::uint32_t kEventRead = EPOLLIN;
	static constexpr std::uint32_t kEventWrite = EPOLLOUT;

	HandleInfo handle_info{};
	int fd{-1};
	std::uint32_t flags{0};
};
} // namespace rac

#endif