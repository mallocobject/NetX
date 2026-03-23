#include "elog/logger.h"
#include "rac/async/async_main.hpp"
#include "rac/async/task.hpp"
#include "rac/net/inet_addr.hpp"
#include "rac/net/socket.hpp"
#include "rac/net/stream.hpp"
#include "rac/rpc/rpc_header.hpp"
#include "rac/rpc/serialize_traits.hpp"
#include <cstdint>
#include <sys/socket.h>

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

Task<> connect_server(int fd)
{
	Stream s{fd};

	using ArgsType = std::tuple<User>;
	ArgsType args{{123, "Alice", {1.0, 2.0}}};

	Buffer payload;
	SerializeTraits<ArgsType>::serialize(&payload, args);

	RpcHeader h{.magic = kMagic,
				.version = kVersion,
				.flags = 1,
				.header_len = kRpcHeaderWireLength,
				.body_len = 0,
				.request_id = 0,
				.reserved = 0};

	h.body_len = payload.readableBytes();

	s.write_buffer()->appendRpcHeader(h);
	s.write_buffer()->append(payload.peek(), payload.readableBytes());
	co_await s.write();

	while (s.read_buffer()->readableBytes() < sizeof(RpcHeaderWire))
	{
		auto rd_buf = co_await s.read();
		if (!rd_buf)
		{
			co_return;
		}
	}

	h = s.read_buffer()->peekRpcHeader();
	std::size_t total_len = h.header_len + h.body_len;
	LOG_DEBUG << total_len;

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

	using RetType = std::tuple<User>;
	RetType ret;

	DeserializeTraits<ArgsType>::deserialize(s.read_buffer(), &ret);

	auto usr = std::get<0>(ret);

	std::cout << "ID: " << usr.id << "\n";
	std::cout << "Name: " << usr.name << "\n";
	std::cout << "Location: (" << usr.loc.x << ", " << usr.loc.y << ")\n";
}

int main()
{
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

	InetAddr addr{8080, true};

	int ret = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

	while (!Socket::connect(fd, addr, nullptr))
	{
	}

	async_main(connect_server(fd));
}