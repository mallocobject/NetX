#ifndef RAC_NET_STREAM_HPP
#define RAC_NET_STREAM_HPP

#include "rac/async/check_error.hpp"
#include "rac/async/event.hpp"
#include "rac/async/event_loop.hpp"
#include "rac/async/task.hpp"
#include "rac/net/buffer.hpp"
#include "rac/net/inet_addr.hpp"
#include "rac/net/socket.hpp"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
namespace rac
{
class Stream
{
	inline static constexpr std::size_t kChunkSize = 4 * 1024;

  public:
	explicit Stream(int fd) : read_fd_(fd), write_fd_(dup(fd))
	{
		assert(fd >= 0);
		Socket::getSockname(read_fd_, &sock_addr_);
		Socket::setNonBlocking(read_fd_);
		Socket::setNonBlocking(write_fd_);
	}

	Stream(int fd, const InetAddr& sock_addr)
		: read_fd_(fd), write_fd_(dup(fd)), sock_addr_(sock_addr)
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

	Task<Buffer*> read()
	{
		while (true)
		{
			int saved_errno = 0;
			ssize_t n = read_buf_.read_fd(read_fd_, &saved_errno);

			if (n > 0)
			{
				co_return &read_buf_;
			}
			else if (n == 0)
			{
				co_return nullptr;
			}
			else
			{
				errno = saved_errno;
				int err = checkErrorNonBlock<ECONNRESET>(n);
				if (err == ECONNRESET)
				{
					co_return nullptr; // 连接已死
				}
				co_await read_awaiter_;
			}
		}
	}

	Task<> write(const std::string& str)
	{
		write_buf_.append(str);
		while (write_buf_.readableBytes() > 0)
		{
			ssize_t n = ::write(write_fd_, write_buf_.peek(),
								write_buf_.readableBytes());
			if (n > 0)
			{
				write_buf_.retrieve(n);
			}
			else if (n < 0)
			{
				int err = checkErrorNonBlock<EPIPE, ECONNRESET>(n);
				if (err == EPIPE || err == ECONNRESET)
				{
					co_return;
				}
				co_await write_awaiter_;
			}
		}
		co_return;
	}

	const InetAddr& sock_addr() const
	{
		return sock_addr_;
	}

  private:
	int read_fd_{-1};
	int write_fd_{-1};
	Buffer read_buf_{};
	Buffer write_buf_{};
	Event read_ev_{.fd = read_fd_, .flags = Event::kEventRead};
	Event write_ev_{.fd = write_fd_, .flags = Event::kEventWrite};
	EventLoop::EventAwaiter read_awaiter_{
		EventLoop::loop().wait_event(read_ev_)};
	EventLoop::EventAwaiter write_awaiter_{
		EventLoop::loop().wait_event(write_ev_)};
	InetAddr sock_addr_{};
};
} // namespace rac

#endif