#include "elog/logger.h"
#include "rac/async/async_main.hpp"
#include "rac/async/check_error.hpp"
#include "rac/async/event.hpp"
#include "rac/async/event_loop.hpp"
#include "rac/async/scheduled_task.hpp"
#include "rac/async/task.hpp"
#include "rac/net/stream.hpp"
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <list>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <queue>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace rac;

Task<> handle_client(int fd)
{
	Stream s(fd);
	const char* response = "HTTP/1.1 200 OK\r\n"
						   "Content-Type: text/plain\r\n"
						   "Content-Length: 12\r\n"
						   "Connection: keep-alive\r\n\r\n"
						   "Hello, world";

	// auto rd_buf = co_await s.read();
	// if (!rd_buf)
	// {
	// 	co_return;
	// }
	// rd_buf->retrieve(rd_buf->readableBytes());
	// co_await s.write(response);

	// co_return;

	while (true)
	{
		auto rd_buf = co_await s.read();
		if (!rd_buf)
		{
			co_return;
		}

		rd_buf->retrieve(rd_buf->readableBytes());
		co_await s.write(response);
	}
}

Task<> server_loop(int listen_fd)
{
	std::list<ScheduledTask<Task<>>> connections;
	Event ev{.fd = listen_fd, .flags = EPOLLIN};
	auto ev_awaiter = EventLoop::loop().wait_event(ev);
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
			connections.emplace_back(handle_client(conn_fd));
		}

		if (connections.size() < 100) [[likely]]
		{
			continue;
		}
		for (auto iter = connections.begin(); iter != connections.end();)
		{
			if (iter->done())
			{
				iter->result(); //< consume result, such as throw exception
				iter = connections.erase(iter);
			}
			else
			{
				++iter;
			}
		}
	}
	co_return;
}

int main()
{
	::signal(SIGPIPE, SIG_IGN);

	int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	int opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = ::htons(8080);
	::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);

	int ret =
		::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
	::listen(listen_fd, SOMAXCONN);

	LOG_WARN << "Server listening on http://127.0.0.1:8080";

	async_main(server_loop(listen_fd));
}