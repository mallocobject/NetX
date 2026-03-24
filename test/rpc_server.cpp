#include "rac/async/async_main.hpp"
#include "rac/async/scheduled_task.hpp"
#include "rac/async/task.hpp"
#include "rac/meta/reflection.hpp"
#include "rac/net/socket.hpp"
#include "rac/net/stream.hpp"
#include "rac/rpc/dispatcher.hpp"
#include "rac/rpc/rpc_header.hpp"
#include "rac/rpc/serialize_traits.hpp"
#include <csignal>
#include <cstddef>
#include <list>
#include <string>
#include <tuple>

using namespace rac;

struct Point
{
	double x;
	double y;
};

struct User
{
	std::uint32_t id;
	std::string name;
	Point loc;
};

User rpc_func(const User& usr)
{
	// 在服务端打印接收到的信息，验证是否正确反序列化了前 3 个字段
	std::cout << "\n--- Server Received --- \n";
	std::cout << "ID: " << usr.id << "\n";
	std::cout << "Name: " << usr.name << "\n";
	std::cout << "Location: (" << usr.loc.x << ", " << usr.loc.y << ")\n";
	std::cout << "-----------------------\n";
	return usr;
}

RpcDispatcher dispatcher;

Task<> handle_client(int fd)
{
	Stream s{fd};

	while (true)
	{
		while (s.read_buffer()->readableBytes() < sizeof(RpcHeaderWire))
		{
			auto rd_buf = co_await s.read();
			if (!rd_buf)
			{
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
				co_return;
			}
		}

		s.read_buffer()->retrieve(h.header_len);

		std::cout << h << std::endl;

		std::string method_name;
		DeserializeTraits<std::string>::deserialize(s.read_buffer(),
													&method_name);

		LOG_DEBUG << "Client requesting method: " << method_name;

		std::size_t size_before = s.write_buffer()->readableBytes();
		dispatcher.dispatch(method_name, s.read_buffer(), s.write_buffer());

		h.body_len = static_cast<uint32_t>(s.write_buffer()->readableBytes() -
										   size_before);
		s.write_buffer()->prependRpcHeader(h);

		co_await s.write();
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
	dispatcher.bind("EchoUser", [](const User& usr) { return usr; });

	dispatcher.bind("AddPoint", [](Point a, Point b)
					{ return Point{a.x + b.x, a.y + b.y}; });

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