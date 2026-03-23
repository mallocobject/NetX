#include "rac/async/async_main.hpp"
#include "rac/async/scheduled_task.hpp"
#include "rac/async/task.hpp"
#include "rac/meta/reflection.hpp"
#include "rac/net/socket.hpp"
#include "rac/net/stream.hpp"
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
	return usr;
}

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

		using ArgsType = std::tuple<User>;
		ArgsType args;

		DeserializeTraits<ArgsType>::deserialize(s.read_buffer(), &args);

		User result = std::apply(rpc_func, args);

		using RetType = std::tuple<User>;
		RetType ret{result};
		Buffer payload;
		SerializeTraits<RetType>::serialize(&payload, ret);

		h.body_len = static_cast<uint32_t>(payload.readableBytes());
		s.write_buffer()->appendRpcHeader(h);
		s.write_buffer()->append(payload.peek(), payload.readableBytes());
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