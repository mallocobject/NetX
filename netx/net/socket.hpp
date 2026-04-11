#ifndef NETX_NET_SOCKET_HPP
#define NETX_NET_SOCKET_HPP

#include "netx/core/error.hpp"
#include "netx/core/expected.hpp"
#include "netx/net/address.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
namespace netx
{
namespace net
{
namespace details
{
namespace Socket
{
// only for connecting, because it is deferred
inline core::Expected<int> check_socket_error(int fd)
{
	int error = 0;
	socklen_t len = sizeof(error);
	if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1)
	{
		return core::details::from_errno(errno);
	}

	return {error};
}

inline core::Expected<int> socket()
{
	int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd == -1)
	{
		return core::details::from_errno(errno);
	}
	return {fd};
}

inline core::Expected<> set_non_blocking(int fd, bool on = true)
{
	int flags = ::fcntl(fd, F_GETFL, 0);
	if (flags == -1)
	{
		return core::details::from_errno(errno);
	}

	if (on)
	{
		flags |= O_NONBLOCK;
	}
	else
	{
		flags &= ~O_NONBLOCK;
	}

	if (::fcntl(fd, F_SETFL, flags) == -1)
	{
		return core::details::from_errno(errno);
	}

	return {};
}

inline core::Expected<> set_reuse_addr(int fd, bool on = true)
{

	int optval = on ? 1 : 0;
	if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) ==
		-1)
	{
		return core::details::from_errno(errno);
	}

	return {};
}

inline core::Expected<> set_keep_alive(int fd, bool on = true)
{
	int optval = on ? 1 : 0;
	if (::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) ==
		-1)
	{
		return core::details::from_errno(errno);
	}

	return {};
}

inline core::Expected<> set_no_delay(int fd, bool on = true)
{
	int optval = on ? 1 : 0;
	if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) ==
		-1)
	{
		return core::details::from_errno(errno);
	}

	return {};
}

inline core::Expected<> bind(int fd, const Address& local_addr)
{
	if (::bind(fd, local_addr.sockaddr(), sizeof(struct sockaddr_in)) == -1)
	{
		return core::details::from_errno(errno);
	}

	return {};
}

inline core::Expected<> listen(int fd, int backlog = SOMAXCONN)
{

	if (::listen(fd, backlog) == -1)
	{
		return core::details::from_errno(errno);
	}

	return {};
}

inline core::Expected<int> accept(int fd, Address* peer_addr)
{
	socklen_t addr_len = sizeof(struct sockaddr_in);
	int conn_fd = ::accept4(fd, peer_addr ? peer_addr->sockaddr() : nullptr,
							peer_addr ? &addr_len : nullptr,
							SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (conn_fd == -1)
	{
		return core::details::from_errno(errno);
	}

	return {conn_fd};
}

inline core::Expected<> connect(int fd, const Address& serv_addr)
{
	if (::connect(fd, serv_addr.sockaddr(), sizeof(struct sockaddr_in)) == -1)
	{
		return core::details::from_errno(errno);
	}
	return {};
}

inline core::Expected<> shutdown(int fd)
{
	if (::shutdown(fd, SHUT_WR) == -1)
	{
		return core::details::from_errno(errno);
	}

	return {};
}

inline core::Expected<> close(int fd)
{
	if (::close(fd) == -1)
	{
		return core::details::from_errno(errno);
	}

	return {};
}

inline core::Expected<Address> get_socket_name(int fd)
{
	Address sock_addr{};
	socklen_t len = sizeof(struct sockaddr_in);
	if (::getsockname(fd, sock_addr.sockaddr(), &len) == -1)
	{
		return core::details::from_errno(errno);
	}

	return {sock_addr};
}

inline core::Expected<Address> get_peer_name(int fd)
{
	Address sock_addr{};
	socklen_t len = sizeof(struct sockaddr_in);
	if (::getpeername(fd, sock_addr.sockaddr(), &len) == -1)
	{
		return core::details::from_errno(errno);
	}

	return {sock_addr};
}
} // namespace Socket
} // namespace details
} // namespace net
} // namespace netx

#endif