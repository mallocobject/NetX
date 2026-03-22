#include "rac/async/async_main.hpp"
#include "rac/async/task.hpp"
#include "rac/net/inet_addr.hpp"
#include "rac/net/rpc_header.hpp"
#include "rac/net/socket.hpp"
#include "rac/net/stream.hpp"
#include <sys/socket.h>

using namespace rac;

Task<> connect_server(int fd)
{
	Stream s{fd};

	RpcHeader h{.magic = kMagic,
				.version = kVersion,
				.flags = 1,
				.header_len = 24,
				.body_len = 0,
				.request_id = 0,
				.reserved = 0};

	s.write_buffer()->appendRpcHeader(h);
	co_await s.write();
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