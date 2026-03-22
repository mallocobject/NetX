#include "rac/async/async_main.hpp"
#include "rac/async/scheduled_task.hpp"
#include "rac/async/task.hpp"
#include "rac/net/rpc_header.hpp"
#include "rac/net/socket.hpp"
#include "rac/net/stream.hpp"
#include <csignal>
#include <list>

using namespace rac;

Task<> handle_client(int fd)
{
	Stream s{fd};

	while (true)
	{
		auto rd_buf = co_await s.read();
		if (!rd_buf)
		{
			co_return;
		}

		if (rd_buf->readableBytes() < sizeof(rac::RpcHeaderWire))
		{
			continue;
		}
		RpcHeader h = rd_buf->retrieveRpcHeader();
		std::cout << h << std::endl;
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

	InetAddr addr{8080, true};

	Socket::bind(listen_fd, addr, nullptr);
	Socket::listen(listen_fd, nullptr);

	LOG_WARN << "Server listening on http://127.0.0.1:8080";

	async_main(server_loop(listen_fd));
}