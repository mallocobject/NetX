#ifndef RAC_RPC_SERVER_HPP
#define RAC_RPC_SERVER_HPP

#include "rac/async/async_main.hpp"
#include "rac/async/check_error.hpp"
#include "rac/async/epoll_poller.hpp"
#include "rac/async/event.hpp"
#include "rac/async/event_loop.hpp"
#include "rac/async/scheduled_task.hpp"
#include "rac/async/task.hpp"
#include "rac/net/inet_addr.hpp"
#include "rac/net/socket.hpp"
#include "rac/net/stream.hpp"
#include "rac/rpc/dispatcher.hpp"
#include <cstddef>
#include <cstdint>
#include <latch>
#include <list>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
namespace rac
{
// RpcServer server("127.0.0.1", 8080);
// server.bind("Echo", [](const User& u) { return u; });
// server.start();

class RpcServer
{
  public:
	explicit RpcServer(const InetAddr& sock_addr)
		: stream_(checkError(Socket::socket(nullptr)), sock_addr)
	{
		int listen_fd = stream_.fd();
		Socket::setReuseAddr(listen_fd, true);
		checkError(Socket::bind(listen_fd, sock_addr, nullptr));
		checkError(Socket::listen(listen_fd, nullptr));
	}

	RpcServer(const std::string& ip, std::uint16_t port)
		: RpcServer(InetAddr{ip, port})
	{
	}

	RpcServer(RpcServer&&) = delete;

	RpcServer& loop(std::size_t loop_count)
	{
		loop_count_ = loop_count;
		return *this;
	}

	template <typename Func>
	RpcServer& bind(const std::string& method_name, Func&& f)
	{
		dispatcher_.bind(method_name, std::forward<Func>(f));
		return *this;
	}

	void start()
	{
		epfds_.reserve(loop_count_);
		std::latch start_latch{static_cast<std::ptrdiff_t>(loop_count_)};
		for (size_t idx = 0; idx < loop_count_; idx++)
		{
			loops_.emplace_back(
				[this, &start_latch]
				{
					{
						std::lock_guard<std::mutex> lock(mtx_);
						epfds_.push_back(EventLoop::loop().epfd());
					}
					start_latch.count_down();
					EventLoop::loop().run_until_complete();
				});
		}
		start_latch.wait();

		LOG_WARN << "RPC Server listening on "
				 << stream_.sock_addr().to_formatted_string();

		async_main(serverLoop());
	}

  private:
	Task<> handleClient(int conn_fd);

	Task<> serverLoop();

  private:
	Stream stream_;
	RpcDispatcher dispatcher_{};
	std::size_t loop_count_{0};
	std::vector<std::jthread> loops_;
	std::mutex mtx_;
	std::vector<int> epfds_;
};

inline Task<> RpcServer::handleClient(int conn_fd)
{
	Stream s{conn_fd};

	while (true)
	{
		while (s.read_buffer()->readableBytes() < sizeof(RpcHeaderWire))
		{
			auto rd_buf = co_await s.read();
			if (!rd_buf)
			{
				LOG_ERROR << "Connection closed by peer before reading "
							 "header in fd "
						  << stream_.fd();
				co_return;
			}
		}

		RpcHeader h = s.read_buffer()->peekRpcHeader();
		std::size_t total_len = h.header_len + h.body_len;

		while (s.read_buffer()->readableBytes() < total_len)
		{
			auto rd_buf = co_await s.read();
			if (!rd_buf)
			{
				LOG_ERROR << "Connection closed by peer before reading "
							 "body in fd "
						  << stream_.fd();
				co_return;
			}
		}

		s.read_buffer()->retrieve(h.header_len);
		std::size_t readable_before = s.read_buffer()->readableBytes();

		std::string method_name;
		DeserializeTraits<std::string>::deserialize(s.read_buffer(),
													&method_name);

		LOG_DEBUG << "Client requesting method: " << method_name;

		std::size_t consumed =
			readable_before - s.read_buffer()->readableBytes();
		std::uint32_t args_limit = h.body_len - consumed;

		std::size_t size_before = s.write_buffer()->readableBytes();

		dispatcher_.dispatch(method_name, s.read_buffer(), s.write_buffer(),
							 args_limit);

		h.body_len = static_cast<uint32_t>(s.write_buffer()->readableBytes() -
										   size_before);
		s.write_buffer()->prependRpcHeader(h);

		co_await s.write();
	}
}

inline Task<> RpcServer::serverLoop()
{
	// std::list<ScheduledTask<Task<>>> connections;
	int listen_fd = stream_.fd();
	Event ev{.fd = listen_fd, .flags = EPOLLIN};
	auto ev_awaiter = EventLoop::loop().wait_event(ev);
	static std::size_t lucky_boy = 0;

	while (true)
	{
		co_await ev_awaiter;
		while (true)
		{
			int conn_fd = accept4(listen_fd, nullptr, nullptr,
								  SOCK_NONBLOCK | SOCK_CLOEXEC);
			if (conn_fd == -1)
			{
				break;
			}
			int opt = 1;
			setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

			int epfd = epfds_[lucky_boy++ % epfds_.size()];

			auto* info = new HandleInfo{.boostrap_fd = conn_fd};

			epoll_event ev{.events = EPOLLIN | EPOLLONESHOT,
						   .data{.ptr = info}};

			checkError(epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev));
			// connections.emplace_back(handleClient(conn_fd));
		}

		// if (connections.size() < 100) [[likely]]
		// {
		// 	continue;
		// }
		// for (auto iter = connections.begin(); iter != connections.end();)
		// {
		// 	if (iter->done())
		// 	{
		// 		iter->result(); //< consume result, such as throw exception
		// 		iter = connections.erase(iter);
		// 	}
		// 	else
		// 	{
		// 		++iter;
		// 	}
		// }
	}
	co_return;
}
} // namespace rac

#endif