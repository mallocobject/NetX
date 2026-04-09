#pragma once

#include "netx/core/handle.hpp"
#include <cstdint>
#include <sys/epoll.h>
namespace netx
{
namespace core
{
struct Event
{
	static constexpr std::uint32_t kEventRead{EPOLLIN};
	static constexpr std::uint32_t kEventWrite{EPOLLOUT};

	HandleInfo info{};
	int fd{-1};
	std::uint32_t flags{0};
};
} // namespace core
} // namespace netx