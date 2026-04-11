#pragma once

#include "elog/logger.hpp"
#include "netx/core/async_main.hpp"
#include "netx/core/error.hpp"
#include "netx/core/event.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/net/address.hpp"
#include "netx/net/scheduler.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"
#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <latch>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>
namespace netx
{
namespace net
{
namespace details
{
template <typename Derived> struct Server
{
	Server(Server&&) = delete;

	Derived& listen(const Address& sock_addr)
	{
		if (sticky_error_)
		{
			return *static_cast<Derived*>(this);
		}

		int listen_fd = stream_.read_fd;

		auto check = [&](auto&& exp) -> bool
		{
			if (!exp)
			{
				sticky_error_ = exp.error();
				elog::LOG_ERROR("Listen failed at {}: {}, {}",
								sock_addr.to_formatted_string(),
								sticky_error_.value(), sticky_error_.message());
				return false;
			}
			return true;
		};

		if (!check(Socket::set_reuse_addr(listen_fd)))
		{
			return *static_cast<Derived*>(this);
		}
		if (!check(Socket::bind(listen_fd, sock_addr)))
		{
			return *static_cast<Derived*>(this);
		}
		if (!check(Socket::listen(listen_fd)))
		{
			return *static_cast<Derived*>(this);
		}

		stream_.bind(sock_addr);
		return *static_cast<Derived*>(this);
	}

	Derived& listen(const std::string& ip, uint16_t port)
	{
		return listen(Address{ip, port});
	}

	Derived& listen(uint16_t port)
	{
		return listen(Address{port});
	}

	template <typename Rep, typename Period>
	Derived& timeout(std::chrono::duration<Rep, Period> duration)
	{
		if (sticky_error_)
		{
			return *static_cast<Derived*>(this);
		}

		timeout_ =
			std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
		return *static_cast<Derived*>(this);
	}

	Derived& loop(size_t loop_count = 1)
	{
		if (sticky_error_)
		{
			return *static_cast<Derived*>(this);
		}

		if (loop_count < 1)
		{
			elog::LOG_FATAL(
				"loop count must more than or equal 1, but now is {}",
				loop_count);
			return *static_cast<Derived*>(this);
		}
		loop_count_ = loop_count;
		return *static_cast<Derived*>(this);
	}

	void start();

  protected:
	explicit Server(Stream&& stream, int idle_fd1, int idle_fd2)
		: stream_(std::move(stream)), idle_fd_{idle_fd1, idle_fd2}
	{
	}

	core::Task<core::Expected<>> handle_client(int fd1, int fd2)
	{
		return static_cast<Derived*>(this)->handle_client(fd1, fd2);
	}

	core::Task<core::Expected<>> server_loop();

  protected:
	Stream stream_;
	size_t loop_count_{1};

	std::vector<Scheduler*> schedulers_;
	std::mutex mtx_;
	std::vector<std::jthread> loops_;

	int idle_fd_[2] = {-1, -1};

	std::chrono::nanoseconds timeout_{std::chrono::nanoseconds::max()};

	std::error_code sticky_error_;
};

template <typename Derived> void Server<Derived>::start()
{
	if (sticky_error_)
	{
		elog::LOG_FATAL("Server cannot start due to previous errors: {}",
						sticky_error_.message());
		return;
	}

	signal(SIGPIPE, SIG_IGN);

	std::latch start_latch(loop_count_);
	for (size_t idx = 0; idx < loop_count_; idx++)
	{
		loops_.emplace_back(
			[this, &start_latch]
			{
				auto exp = Scheduler::create();
				if (!exp)
				{
					const std::error_code& ec = exp.error();
					elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
					return;
				}

				Scheduler scheduler = std::move(exp.value());
				{
					std::lock_guard<std::mutex> lock(mtx_);
					schedulers_.push_back(&scheduler);
				}

				core::async_main(scheduler.scheduler_loop(start_latch));
			});
	}

	start_latch.wait();
	elog::LOG_WARN("NetX-Server listening on {}",
				   stream_.sock_addr.to_formatted_string());
	core::async_main(server_loop());
}

template <typename Derived>
core::Task<core::Expected<>> Server<Derived>::server_loop()
{
	int listen_fd = stream_.read_fd;
	auto ev_awaiter =
		core::details::EventLoop::loop().wait_event(core::details::Event{
			.fd = listen_fd, .flags = core::details::Event::kEventRead});
	static size_t lucky_boy = 0;

	while (true)
	{
		if (auto exp = co_await ev_awaiter; !exp)
		{
			const std::error_code& ec = exp.error();
			elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
			co_return {};
		}
		static int emfile_count = 0;
		while (true)
		{
			Address addr;
			auto exp = Socket::accept(listen_fd, &addr);
			if (!exp)
			{
				switch (errno)
				{
				case EAGAIN:
				case EINTR:
				case ECONNABORTED:
					break;
				case EMFILE:
					emfile_count++;
					if (emfile_count > 5)
					{
						std::this_thread::sleep_for(
							std::chrono::milliseconds(10));
						emfile_count = 0;
					}

					Socket::close(idle_fd_[0]);
					Socket::close(idle_fd_[1]);

					if (auto exp2 = Socket::accept(listen_fd, &addr); exp2)
					{
						Socket::close(exp2.value());
					}

					idle_fd_[0] = open("/dev/null", O_RDONLY | O_CLOEXEC);
					idle_fd_[1] = (idle_fd_[0] >= 0) ? dup(idle_fd_[0]) : -1;
					elog::LOG_WARN("EMFILE: Out of file descriptors!");
					break;

				default:
					const std::error_code& ec = exp.error();
					elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
					break;
				}
				break;
			}

			int conn_fd = exp.value();
			int dup_conn_fd = dup(conn_fd);
			if (dup_conn_fd < 0)
			{
				auto ec = core::details::from_errno(errno);
				Socket::close(dup_conn_fd);

				elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
				break;
			}

			int opt = 1;
			setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(int));

			assert(schedulers_.size() >= 1);
			auto lucky = schedulers_[lucky_boy++ % schedulers_.size()];
			lucky->push(handle_client(conn_fd, dup_conn_fd));
			lucky->wakeup();
		}
	}

	co_return {};
}
} // namespace details
} // namespace net
} // namespace netx