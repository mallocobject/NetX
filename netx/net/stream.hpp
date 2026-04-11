#pragma once

#include "netx/core/event.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/net/address.hpp"
#include "netx/net/buffer.hpp"
#include "netx/net/socket.hpp"
#include <cstddef>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
namespace netx
{
namespace net
{
namespace details
{
struct Stream
{
	inline static constexpr size_t kChunkSize = 4 * 1024;

	static core::Expected<Stream> create(int fd)
	{
		if (auto exp = Socket::set_non_blocking(fd); !exp.has_value())
		{
			return exp.error();
		}

		int w_fd = ::dup(fd);
		if (w_fd == -1)
		{
			return core::details::from_errno(errno);
		}

		return Stream{fd, w_fd};
	}

	static core::Expected<Stream> create(int fd1, int fd2)
	{
		if (auto exp = Socket::set_non_blocking(fd1); !exp.has_value())
		{
			return exp.error();
		}

		if (auto exp = Socket::set_non_blocking(fd2); !exp.has_value())
		{
			return exp.error();
		}

		return Stream{fd1, fd2};
	}

	core::Expected<> close()
	{
		if (auto exp = read_awaiter_.reset(); !exp.has_value())
		{
			return exp.error();
		}
		if (auto exp = write_awaiter_.reset(); !exp.has_value())
		{
			return exp.error();
		}

		if (read_fd > 0)
		{
			Socket::close(read_fd);
		}
		if (write_fd > 0)
		{
			Socket::close(write_fd);
		}
		read_fd = write_fd = -1;
		return {};
	}

	core::Expected<> shutdown()
	{
		if (auto exp = write_awaiter_.reset(); !exp.has_value())
		{
			return exp.error();
		}

		if (write_fd > 0)
		{
			if (auto exp = Socket::shutdown(write_fd); !exp.has_value())
			{
				return exp.error();
			}

			Socket::close(write_fd);
		}
		write_fd = -1;

		return {};
	}

	core::Task<core::Expected<size_t>> read();
	core::Task<core::Expected<>> write(std::string_view data = "");

	void bind(const Address& addr)
	{
		sock_addr = addr;
	}

	Stream(Stream&& other) noexcept
		: read_fd(std::exchange(other.read_fd, -1)),
		  write_fd(std::exchange(other.write_fd, -1)),
		  sock_addr(other.sock_addr), read_buf(std::move(other.read_buf)),
		  write_buf(std::move(other.write_buf)),
		  read_awaiter_(std::move(other.read_awaiter_)),
		  write_awaiter_(std::move(other.write_awaiter_))
	{
	}

	~Stream()
	{
		close();
	}

  private:
	explicit Stream(int fd1, int fd2)
		: read_fd(fd1), write_fd(fd2),
		  read_awaiter_(core::details::EventLoop::loop().wait_event(
			  {.fd = read_fd, .flags = core::details::Event::kEventRead})),
		  write_awaiter_(core::details::EventLoop::loop().wait_event(
			  {.fd = write_fd, .flags = core::details::Event::kEventWrite}))

	{
	}

  public:
	int read_fd{-1};
	int write_fd{-1};

	Address sock_addr{};

	Buffer read_buf{};
	Buffer write_buf{};

  private:
	core::details::EventLoop::EventAwaiter read_awaiter_;
	core::details::EventLoop::EventAwaiter write_awaiter_;
};

inline core::Task<core::Expected<size_t>> Stream::read()
{
	while (true)
	{
		ssize_t n = co_await read_buf.read_fd(read_fd);
		if (n == -1)
		{
			co_await co_await read_awaiter_;
			continue;
		}
		co_return n;
	}
}

inline core::Task<core::Expected<>> Stream::write(std::string_view data)
{
	write_buf.append(data);
	while (write_buf.readable_bytes() > 0)
	{
		ssize_t n =
			::write(write_fd, write_buf.peek(), write_buf.readable_bytes());
		if (n > 0)
		{
			write_buf.retrieve(n);
		}
		else if (n < 0)
		{
			if (errno == EWOULDBLOCK || errno == EAGAIN)
			{
				co_await co_await read_awaiter_;
				continue;
			}
			co_return core::details::from_errno(errno);
		}
	}

	co_return {};
}
} // namespace details
} // namespace net
} // namespace netx