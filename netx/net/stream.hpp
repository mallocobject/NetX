#ifndef NETX_NET_STREAM_HPP
#define NETX_NET_STREAM_HPP

#include "netx/async/check_error.hpp"
#include "netx/async/event.hpp"
#include "netx/async/event_loop.hpp"
#include "netx/async/task.hpp"
#include "netx/net/buffer.hpp"
#include "netx/net/inet_addr.hpp"
#include "netx/net/socket.hpp"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
namespace netx
{
namespace net
{
namespace async = netx::async;
class Stream
{
	inline static constexpr std::size_t kChunkSize = 4 * 1024;

  public:
	explicit Stream(int fd) : read_fd_(fd), write_fd_(dup(fd))
	{
		assert(read_fd_ != -1 && write_fd_ != -1);
		// Socket::getSockname(read_fd_, &sock_addr_);
		Socket::setNonBlocking(read_fd_);
		Socket::setNonBlocking(write_fd_);
	}

	explicit Stream(int read_fd, int write_fd)
		: read_fd_(read_fd), write_fd_(write_fd)
	{
		assert(read_fd_ != -1 && write_fd_ != -1);
		// Socket::getSockname(read_fd_, &sock_addr_);
		Socket::setNonBlocking(read_fd_);
		Socket::setNonBlocking(write_fd_);
	}

	Stream(int fd, InetAddr sock_addr)
		: read_fd_(fd), write_fd_(dup(fd)), sock_addr_(std::move(sock_addr))
	{

		Socket::setNonBlocking(read_fd_);
		Socket::setNonBlocking(write_fd_);
	}

	Stream(Stream&& other) noexcept
		: read_fd_(std::exchange(other.read_fd_, -1)),
		  write_fd_(std::exchange(other.write_fd_, -1)),
		  read_ev_(std::exchange(other.read_ev_, {})),
		  write_ev_(std::exchange(other.write_ev_, {})),
		  read_awaiter_(std::move(other.read_awaiter_)),
		  write_awaiter_(std::move(other.write_awaiter_)),
		  sock_addr_(std::exchange(other.sock_addr_, {}))
	{
	}

	~Stream()
	{
		close();
	}

	void bind(InetAddr sock_addr)
	{
		sock_addr_ = std::move(sock_addr);
	}

	void close()
	{
		read_awaiter_.reset();
		write_awaiter_.reset();
		if (read_fd_ > 0)
		{
			Socket::close(read_fd_);
		}
		if (write_fd_ > 0)
		{
			Socket::close(write_fd_);
		}
		read_fd_ = write_fd_ = -1;
	}

	void shutdown()
	{
		write_awaiter_.reset();
		if (write_fd_ > 0)
		{
			Socket::shutdown(write_fd_);
		}
		write_fd_ = -1;
	}

	async::Task<bool> read();

	async::Task<bool> write(const std::string& str = std::string());

	const InetAddr& sock_addr() const
	{
		return sock_addr_;
	}

	Buffer* read_buffer()
	{
		return &read_buf_;
	}

	Buffer* write_buffer()
	{
		return &write_buf_;
	}

	int read_fd() const noexcept
	{
		return read_fd_;
	}

	int write_fd() const noexcept
	{
		return write_fd_;
	}

  private:
	int read_fd_{-1};
	int write_fd_{-1};
	Buffer read_buf_{};
	Buffer write_buf_{};
	async::Event read_ev_{.fd = read_fd_, .flags = async::Event::kEventRead};
	async::Event write_ev_{.fd = write_fd_, .flags = async::Event::kEventWrite};
	async::EventLoop::EventAwaiter read_awaiter_{
		async::EventLoop::loop().wait_event(read_ev_)};
	async::EventLoop::EventAwaiter write_awaiter_{
		async::EventLoop::loop().wait_event(write_ev_)};
	InetAddr sock_addr_{};
};

inline async::Task<bool> Stream::read()
{
	while (true)
	{
		int saved_errno = 0;
		ssize_t n = read_buf_.read_fd(read_fd_, &saved_errno);

		if (n > 0)
		{
			co_return true;
		}
		else if (n == 0)
		{
			co_return false;
		}
		else
		{
			errno = saved_errno;
			int err = async::checkErrorNonBlock<ECONNRESET>(n);
			if (err == ECONNRESET)
			{
				co_return false; // 连接已死
			}
			co_await read_awaiter_;
		}
	}
}

inline async::Task<bool> Stream::write(const std::string& str)
{
	write_buf_.append(str);
	while (write_buf_.readableBytes() > 0)
	{
		// ssize_t n = ::send(write_fd_, write_buf_.peek(),
		// 				   write_buf_.readableBytes(), MSG_NOSIGNAL);
		ssize_t n =
			::write(write_fd_, write_buf_.peek(), write_buf_.readableBytes());
		if (n > 0)
		{
			write_buf_.retrieve(n);
		}
		else if (n < 0)
		{
			int err = async::checkErrorNonBlock<EPIPE, ECONNRESET>(n);
			if (err == EPIPE || err == ECONNRESET)
			{
				co_return false;
			}
			co_await write_awaiter_;
		}
	}
	co_return true;
}
} // namespace net
} // namespace netx

#endif