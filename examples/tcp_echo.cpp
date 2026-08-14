#include "elog/logger.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/net/server.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"
#include <fcntl.h>
#include <unistd.h>

using namespace netx::core;

namespace net = netx::net::details;

// 原始 TCP 层示例:直接继承 net::details::Server 实现 echo 服务,
// 展示 NetX 在 HTTP 之下的通用网络层。
struct TcpEchoServer : netx::net::details::Server<TcpEchoServer>
{
	static TcpEchoServer& instance()
	{
		int fd1 = open("/dev/null", O_RDONLY | O_CLOEXEC);
		int fd2 = (fd1 >= 0) ? dup(fd1) : -1;
		int fd = net::Socket::socket().value();
		static TcpEchoServer srv{net::Stream::create(fd).value(), fd1, fd2};
		return srv;
	}

	Task<Expected<>> handle_client(int read_fd, int write_fd)
	{
		auto stream_exp = net::Stream::create(read_fd, write_fd);
		if (!stream_exp)
		{
			::close(read_fd);
			::close(write_fd);
			co_return {};
		}

		net::Stream s = std::move(stream_exp.value());
		elog::LOG_DEBUG("echo client connected fd={}", read_fd);

		while (true)
		{
			auto n_exp = co_await s.read();
			if (!n_exp)
			{
				break; // 对端关闭或出错
			}

			std::string data = s.read_buf.retrieve_string(n_exp.value());
			if (auto w_exp = co_await s.write(data); !w_exp)
			{
				break;
			}
		}

		s.shutdown();
		elog::LOG_DEBUG("echo client disconnected fd={}", read_fd);
		co_return {};
	}

  private:
	TcpEchoServer(net::Stream&& stream, int idle_fd1, int idle_fd2)
		: netx::net::details::Server<TcpEchoServer>(std::move(stream), idle_fd1,
													idle_fd2)
	{
	}
};

int main()
{
	TcpEchoServer::instance()
		.listen("0.0.0.0", 8082)
		.loop(4)
		.start();
}
