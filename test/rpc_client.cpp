#include "elog/logger.h"
#include "rac/async/async_main.hpp"
#include "rac/async/task.hpp"
#include "rac/net/inet_addr.hpp"
#include "rac/net/socket.hpp"
#include "rac/net/stream.hpp"
#include "rac/rpc/rpc_header.hpp"
#include "rac/rpc/serialize_traits.hpp"
#include <cstddef>
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

	std::string email;
	std::uint64_t phone_number;
};

Task<> connect_server(int fd)
{
	Stream s{fd};

	// Buffer payload;

	std::size_t size_before = s.write_buffer()->readableBytes();
	std::string method_name = "AddPoint";
	SerializeTraits<std::string>::serialize(s.write_buffer(), method_name);

	using ArgsType = std::tuple<Point, Point>;
	// ArgsType args{{123, "Alice", {1.0, 2.0}, "alice@github.com",
	// 13800138000}};
	ArgsType args{{1.2, 3.6}, {5.8, 9.2}};

	SerializeTraits<ArgsType>::serialize(s.write_buffer(), args);

	RpcHeader h{.magic = kMagic,
				.version = kVersion,
				.flags = 1,
				.header_len = kRpcHeaderWireLength,
				.body_len = 0,
				.request_id = 0,
				.reserved = 0};

	h.body_len = static_cast<std::uint32_t>(s.write_buffer()->readableBytes() -
											size_before);

	s.write_buffer()->prependRpcHeader(h);

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

	using RetType = std::tuple<Point>;
	RetType ret;

	DeserializeTraits<RetType>::deserialize(s.read_buffer(), &ret);

	auto& res = std::get<0>(ret);

	// std::cout << "ID: " << usr.id << "\n";
	// std::cout << "Name: " << usr.name << "\n";
	// std::cout << "Location: (" << usr.loc.x << ", " << usr.loc.y << ")\n";
	std::cout << "x: " << res.x << std::endl;
	std::cout << "y: " << res.y << std::endl;
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