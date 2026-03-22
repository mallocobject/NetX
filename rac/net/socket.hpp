#ifndef RAC_NET_SOCKET_HPP
#define RAC_NET_SOCKET_HPP

#include "elog/logger.h"
#include "rac/net/inet_addr.hpp"
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
namespace rac
{
namespace Socket
{
// only for connecting, because it is deferred
inline int socketErrno(int fd)
{
	int error = 0;
	socklen_t len = sizeof(error);
	if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1)
	{
		LOG_ERROR << "getsockopt(SO_ERROR) failed for fd " << fd << ": "
				  << ::strerror(errno);
		return -1;
	}
	return error;
}

inline int socket(int* saved_errno)
{
	if (saved_errno)
	{
		*saved_errno = 0;
	}

	int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd == -1)
	{
		if (saved_errno)
		{
			*saved_errno = errno;
		}
		LOG_ERROR << "create socket failed: " << ::strerror(errno);
	}
	return fd;
}

inline void setNonBlocking(int fd, bool on = true)
{
	{
		int flags = ::fcntl(fd, F_GETFL, 0);
		if (flags == -1)
		{
			LOG_ERROR << "fcntl(F_GETFL) failed for fd " << fd << ": "
					  << ::strerror(errno);
			return;
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
			LOG_ERROR << "fcntl(F_SETFL) failed for fd " << fd << ": "
					  << ::strerror(errno);
		}
	}
}

inline void setReuseAddr(int fd, bool on = true)
{
	{
		int optval = on ? 1 : 0;
		if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval,
						 sizeof(optval)) == -1)
		{
			LOG_ERROR << "setsockopt(SO_REUSEADDR) failed for fd " << fd << ": "
					  << ::strerror(errno);
		}
	}
}

inline void setKeepAlive(int fd, bool on = true)
{
	int optval = on ? 1 : 0;
	if (::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) ==
		-1)
	{
		LOG_ERROR << "setsockopt(SO_KEEPALIVE) failed for fd " << fd << ": "
				  << ::strerror(errno);
	}
}

inline void setNoDelay(int fd, bool on = true)
{
	int optval = on ? 1 : 0;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)))
	{
		LOG_ERROR << "setsockopt(TCP_NODELAY) failed for fd " << fd << ": "
				  << ::strerror(errno);
	}
}

inline bool bind(int fd, const InetAddr& local_addr, int* saved_errno)
{
	if (saved_errno)
	{
		*saved_errno = 0;
	}

	if (::bind(fd, local_addr.sockaddr(), sizeof(struct sockaddr_in)) == -1)
	{
		if (saved_errno)
		{
			*saved_errno = errno;
		}
		LOG_ERROR << "bind failed for fd " << fd << ": " << ::strerror(errno);
		return false;
	}
	return true;
}

inline bool listen(int fd, int* saved_errno, int backlog = SOMAXCONN)
{
	if (saved_errno)
	{
		*saved_errno = 0;
	}

	if (::listen(fd, backlog) == -1)
	{
		if (saved_errno)
		{
			*saved_errno = errno;
		}
		LOG_ERROR << "listen failed for fd " << fd << ": " << ::strerror(errno);
		return false;
	}
	return true;
}

inline int accept(int fd, InetAddr* peer_addr, int* saved_errno)
{
	if (saved_errno)
	{
		*saved_errno = 0;
	}
	socklen_t addr_len = sizeof(struct sockaddr_in);
	int conn_fd = ::accept4(fd, peer_addr ? peer_addr->sockaddr() : nullptr,
							&addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (conn_fd == -1)
	{
		if (saved_errno)
		{
			*saved_errno = errno;
		}
	}

	return conn_fd;
}

inline bool connect(int fd, const InetAddr& serv_addr, int* saved_errno)
{
	if (saved_errno)
	{
		*saved_errno = 0;
	}

	if (::connect(fd, serv_addr.sockaddr(), sizeof(sockaddr)) == -1)
	{
		if (saved_errno)
		{
			*saved_errno = socketErrno(fd);
		}
		return false;
	}

	return true;
}

inline void shutdown(int fd)
{
	if (::shutdown(fd, SHUT_WR) == -1)
	{
		LOG_ERROR << "shutdown failed for fd " << fd << ": "
				  << ::strerror(errno);
	}
}

inline void close(int fd)
{
	if (::close(fd) == -1)
	{
		LOG_ERROR << "close failed for fd " << fd << ": " << ::strerror(errno);
	}
}

inline void getSockname(int fd, InetAddr* sock_addr)
{
	socklen_t len = sizeof(struct sockaddr_in);
	if (::getsockname(fd, sock_addr->sockaddr(), &len) == -1)
	{
		LOG_ERROR << "get socket name failed for fd " << fd << ": "
				  << ::strerror(errno);
	}
}

inline void getPeername(int fd, InetAddr* sock_addr)
{
	socklen_t len = sizeof(struct sockaddr_in);
	if (::getpeername(fd, sock_addr->sockaddr(), &len) == -1)
	{
		LOG_ERROR << "get peer socket name failed for fd " << fd << ": "
				  << ::strerror(errno);
	}
}
} // namespace Socket
} // namespace rac

#endif