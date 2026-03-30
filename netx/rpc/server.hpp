#ifndef NETX_RPC_SERVER_HPP
#define NETX_RPC_SERVER_HPP

#include "netx/async/task.hpp"
#include "netx/net/stream.hpp"
#include "netx/rpc/dispatcher.hpp"
#include <cstddef>
#include <cstdint>
#include <netx/net/server.hpp>
#include <utility>
namespace netx
{
namespace rpc
{
// RpcServer server("127.0.0.1", 8080);
// server.bind("Echo", [](const User& u) { return u; });
// server.start();

namespace async = netx::async;
namespace net = netx::net;
class RpcServer : public net::Server<RpcServer>
{
	friend class net::Server<RpcServer>;
	RpcServer() = default;

  public:
	static RpcServer& server()
	{
		static RpcServer rpc_server{};
		return rpc_server;
	}

	template <typename Func>
	RpcServer& bind(const std::string& method_name, Func&& f)
	{
		dispatcher_.bind(method_name, std::forward<Func>(f));
		return *this;
	}

  private:
	async::Task<> handleClient(int read_fd, int write_fd);

	// async::Task<> serverLoop();

  private:
	RpcDispatcher dispatcher_{};
};

inline async::Task<> RpcServer::handleClient(int read_fd, int write_fd)
{
	net::Stream s{read_fd, write_fd};

	try
	{
		while (true)
		{
			while (s.read_buffer()->readableBytes() < sizeof(RpcHeaderWire))
			{
				if (!co_await s.read())
				{
					::elog::LOG_ERROR("Connection closed by peer before "
									  "reading header in fd {}",
									  s.read_fd());

					s.close();
					co_return;
				}
			}

			RpcHeader h = s.read_buffer()->peekRpcHeader();
			std::size_t total_len = h.header_len + h.body_len;

			while (s.read_buffer()->readableBytes() < total_len)
			{
				if (!co_await s.read())
				{
					::elog::LOG_ERROR("Connection closed by peer before "
									  "reading body in fd {}",
									  s.read_fd());

					s.close();
					co_return;
				}
			}

			s.read_buffer()->retrieve(h.header_len);
			std::size_t readable_before = s.read_buffer()->readableBytes();

			std::string method_name;
			DeserializeTraits<std::string>::deserialize(s.read_buffer(),
														&method_name);

			// LOG_DEBUG("Client requesting method: {}" ,method_name);

			std::size_t consumed =
				readable_before - s.read_buffer()->readableBytes();
			std::uint32_t args_limit = h.body_len - consumed;

			std::size_t size_before = s.write_buffer()->readableBytes();

			dispatcher_.dispatch(method_name, s.read_buffer(), s.write_buffer(),
								 args_limit);

			h.body_len = static_cast<uint32_t>(
				s.write_buffer()->readableBytes() - size_before);
			s.write_buffer()->prependRpcHeader(h);

			co_await s.write();
		}
	}
	catch (const std::exception& e)
	{
		elog::LOG_ERROR("Exception in handleClient: {}", e.what());
	}
}
} // namespace rpc
} // namespace netx

#endif