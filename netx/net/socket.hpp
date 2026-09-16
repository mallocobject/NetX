#pragma once

#include "netx/core/expected.hpp"
#include "netx/net/address.hpp"
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace netx::net::details::Socket {

// 取出套接字上挂起的错误。SO_ERROR 是"读后清"语义，读一次就归零。
//
// 非阻塞 connect 的最终结果要靠它拿：connect() 当场只会返回 EINPROGRESS，
// 真正的成败要等套接字可写之后从这里读出来。
inline core::Expected<> check_socket_error(int fd) {
    int error = 0;
    socklen_t len = sizeof(error);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    if (error != 0) {
        // 必须走 unexpected：Expected 的成败只看"有没有值"，把错误码当成值
        // 返回会让连接失败表现成成功。
        return core::details::from_errno_to_unexpected(error);
    }

    return {};
}

// 建出来的是非阻塞 + CLOEXEC 的 TCP 套接字
inline core::Expected<int> socket() {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        return core::details::from_errno_to_unexpected(errno);
    }
    return {fd};
}

inline core::Expected<> set_non_blocking(int fd, bool on = true) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    if (on) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (::fcntl(fd, F_SETFL, flags) == -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {};
}

inline core::Expected<> set_reuse_addr(int fd, bool on = true) {
    int optval = on ? 1 : 0;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) ==
        -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {};
}

inline core::Expected<> set_keep_alive(int fd, bool on = true) {
    int optval = on ? 1 : 0;
    if (::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) ==
        -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {};
}

inline core::Expected<> set_no_delay(int fd, bool on = true) {
    int optval = on ? 1 : 0;
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) ==
        -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {};
}

inline core::Expected<> bind(int fd, const Address &local_addr) {
    const int rc = with_sockaddr(
        local_addr, [fd](const struct sockaddr *sa, socklen_t len) {
            return ::bind(fd, sa, len);
        });
    if (rc == -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {};
}

inline core::Expected<> listen(int fd, int backlog = SOMAXCONN) {
    if (::listen(fd, backlog) == -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {};
}

// 非阻塞 connect 的语义：
//   当场建成   -> 返回 0
//   正在建立   -> -1 且 errno == EINPROGRESS
// EINPROGRESS 不是失败：这里返回成功只代表"请求已发出"。真正的成败要等套接字
// 可写（成功和失败都会让它可写）之后交给 check_socket_error。
inline core::Expected<> connect(int fd, const Address &serv_addr) {
    const int rc = with_sockaddr(
        serv_addr, [fd](const struct sockaddr *sa, socklen_t len) {
            return ::connect(fd, sa, len);
        });

    if (rc == -1 && errno != EINPROGRESS) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {};
}

// peer_addr 传 nullptr 表示不关心对端地址。
//
// 非阻塞监听套接字上暂时没有待处理连接时返回 EAGAIN/EWOULDBLOCK ——
// 调用方（通常由 epoll 驱动）应理解为"这一轮没有了"而非致命错误；这里不替
// 调用方吞掉，保留原始 errno 让它自行判断。
inline core::Expected<int> accept(int fd, Address *peer_addr = nullptr) {
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);

    const int conn_fd = ::accept4(
        fd,
        peer_addr ? reinterpret_cast<struct sockaddr *>(&peer) : nullptr,
        peer_addr ? &peer_len : nullptr,
        SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (conn_fd == -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    if (peer_addr) {
        *peer_addr = Address{peer};
    }

    return {conn_fd};
}

inline core::Expected<> shutdown(int fd, int how = SHUT_WR) {
    if (::shutdown(fd, how) == -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {};
}

inline core::Expected<> close(int fd) {
    if (::close(fd) == -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {};
}

inline core::Expected<Address> get_socket_name(int fd) {
    sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (::getsockname(fd, reinterpret_cast<struct sockaddr *>(&local), &len) ==
        -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {Address{local}};
}

inline core::Expected<Address> get_peer_name(int fd) {
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    if (::getpeername(fd, reinterpret_cast<struct sockaddr *>(&peer), &len) ==
        -1) {
        return core::details::from_errno_to_unexpected(errno);
    }

    return {Address{peer}};
}
} // namespace netx::net::details::Socket
