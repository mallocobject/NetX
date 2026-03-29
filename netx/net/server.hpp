#ifndef NETX_NET_SERVER_HPP
#define NETX_NET_SERVER_HPP

#include "netx/async/async_main.hpp"
#include "netx/async/check_error.hpp"
#include "netx/async/task.hpp"
#include "netx/net/inet_addr.hpp"
#include "netx/net/scheduler.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"
#include <cstddef>
#include <cstdint>
#include <latch>
#include <thread>
#include <vector>
#include <chrono>
#include <csignal>
#include <mutex>
namespace netx
{
namespace net
{
namespace async = netx::async;
template <typename Derived> class Server
{
  protected:
	Server() : stream_(async::checkError(net::Socket::socket(nullptr)))
	{
		idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
		if (idle_fd_ == -1)
		{
			::elog::LOG_WARN("open /dev/null failed: {}", ::strerror(errno));
		}
	}

  public:
	Server(Server&&) = delete;

	Derived& listen(const net::InetAddr& sock_addr)
	{
		stream_.bind(sock_addr);
		int listen_fd = stream_.read_fd();
		net::Socket::setReuseAddr(listen_fd, true);
		async::checkError(net::Socket::bind(listen_fd, sock_addr, nullptr));
		async::checkError(net::Socket::listen(listen_fd, nullptr));

		return *static_cast<Derived*>(this);
	}

	Derived& listen(const std::string& ip, std::uint16_t port)
	{
		return listen(net::InetAddr{ip, port});
	}

	template <typename Rep, typename Period>
	Derived& timeout(std::chrono::duration<Rep, Period> duration)
	{
		timeout_ = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
		return *static_cast<Derived*>(this);
	}

	Derived& loop(std::size_t loop_count = 1)
	{
		if (loop_count < 1)
		{
			::elog::LOG_FATAL("loop count must be >= 1");
			exit(EXIT_FAILURE);
		}
		loop_count_ = loop_count;
		return *static_cast<Derived*>(this);
	}

	void start()
	{
		::signal(SIGPIPE, SIG_IGN);

		std::latch start_latch{static_cast<std::ptrdiff_t>(loop_count_)};
		for (size_t idx = 0; idx < loop_count_; idx++)
		{
			loops_.emplace_back(
				[this, &start_latch]
				{
					net::Scheduler scheduler{};
					{
						std::lock_guard<std::mutex> lock(schedulers_mutex_);
						schedulers_ptr_.push_back(&scheduler);
					}
					async_main(scheduler.schedulerLoop(start_latch));
				});
		}
		start_latch.wait();

		::elog::LOG_WARN("Netx-Server listening on {}",
                 stream_.sock_addr().to_formatted_string());

		async_main(serverLoop());
	}

  protected:
	async::Task<> handleClient(int conn_fd)
	{
		return static_cast<Derived*>(this)->handleClient(conn_fd);
	}

	async::Task<> serverLoop();

  protected:
	net::Stream stream_;
	std::size_t loop_count_{0};

	std::vector<net::Scheduler*> schedulers_ptr_;
	std::mutex schedulers_mutex_;
	std::vector<std::jthread> loops_;

	int idle_fd_{-1};

	std::chrono::nanoseconds timeout_{std::chrono::nanoseconds::max()};
};

template <typename Derived> inline async::Task<> Server<Derived>::serverLoop()
{
	int listen_fd = stream_.read_fd();
	async::Event ev{.fd = listen_fd, .flags = EPOLLIN};
	auto ev_awaiter = async::EventLoop::loop().wait_event(ev);
	static std::size_t lucky_boy = 0;

	while (true)
	{
		co_await ev_awaiter;
		static int emfile_count = 0;
		while (true)
		{
			int saved_errno = 0;
			int conn_fd = Socket::accept(listen_fd, nullptr, &saved_errno);

			if (conn_fd < 0)
			{
				switch (saved_errno)
				{
				case EAGAIN:
				case EINTR:
				case ECONNABORTED:
					break;
				case EMFILE:
					emfile_count++;
					if (emfile_count > 5) {
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
						emfile_count = 0;
					}
					Socket::close(idle_fd_);
					conn_fd = Socket::accept(listen_fd, nullptr, nullptr);
					Socket::close(conn_fd);
					idle_fd_ = open("/dev/null", O_RDONLY | O_CLOEXEC);
					::elog::LOG_ERROR("EMFILE: Out of file descriptors!");
					break;
				default:
					async::checkError(saved_errno);
					break;
				}
				break;
			}

			int opt = 1;
			setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

			assert(schedulers_ptr_.size() >= 1);
			auto lucky = schedulers_ptr_[lucky_boy++ % schedulers_ptr_.size()];
			lucky->push(handleClient(conn_fd));
			lucky->wakeup();
		}
	}
	co_return;
}
} // namespace net
} // namespace netx

#endif