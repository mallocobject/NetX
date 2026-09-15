#pragma once

#include <cstdint>
#include <sys/epoll.h>

namespace netx::core::details {
struct Event {
    inline static constexpr std::uint32_t kEventRead{EPOLLIN};
    inline static constexpr std::uint32_t kEventWrite{EPOLLOUT};

    int fd{-1};
    std::uint32_t flags{0};
};
} // namespace netx::core::details
