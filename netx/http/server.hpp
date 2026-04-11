#pragma once

#include "netx/core/error.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/sleep.hpp"
#include "netx/core/when_any.hpp"
#include "netx/http/router.hpp"
#include "netx/http/session.hpp"
#include "netx/net/server.hpp"
#include <utility>
namespace netx
{
namespace http
{
class Server : public net::details::Server<Server>
{
	friend class net::details::Server<Server>;
	Server(net::details::Stream&& stream, int idle_fd1, int idle_fd2)
		: net::details::Server<Server>(std::move(stream), idle_fd1, idle_fd2)
	{
	}

  public:
	static Server& server()
	{
		static Server http_server{};
		return http_server;
	}

	template <typename Handler>
	Server& route(const std::string& method, const std::string& path,
				  Handler&& handler)
	{
		router_.route(method, path, std::forward<Handler>(handler));
		return *this;
	}

	// template <typename Handler>
	// Server& ws(const std::string& path, Handler&& handler)
	// {
	// 	router_.route_ws(path, std::forward<Handler>(handler));
	// 	return *this;
	// }

  private:
	core::Task<core::Expected<>> handle_client(int read_fd, int write_fd);

  private:
	details::Router router_{};
};

inline core::Task<core::Expected<>> Server::handle_client(int read_fd,
														  int write_fd)
{

	net::details::Stream s =
		co_await net::details::Stream::create(read_fd, write_fd);
	details::Session session{};

	try
	{
		while (true)
		{
			auto exp = co_await core::when_any(s.read(), core::sleep(timeout_));
			if (s.write_fd == -1)
			{
				co_return {};
			}

			if (!exp.has_value())
			{
				const std::error_code& ec = exp.error();
				if (ec == core::details::Error::Timeout)
				{

					if (auto exp2 =
							co_await s.write("HTTP/1.1 408 Request Timeout\r\n"
											 "Content-Length: 0\r\n"
											 "Connection: close\r\n"
											 "\r\n");
						!exp2.has_value())
					{
						const std::error_code& ec2 = exp.error();
						elog::LOG_ERROR("{}, {}", ec2.value(), ec2.message());
						co_return {};
					}
					s.shutdown();
					co_return {};
				}
				elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
				co_return {};
			}

			auto ret = std::move(exp.value());
			if (!std::get<0>(ret))
			{
				co_return {};
			}

			while (s.read_buf.readable_bytes() > 0)
			{
				if (!session.parse(s.read_buf))
				{
					co_await s.write("HTTP/1.1 400 Bad Request\r\n"
									 "Content-Length: 0\r\n"
									 "Connection: close\r\n"
									 "\r\n");
					s.shutdown();
					co_return {};
				}

				if (session.completed())
				{
					auto& req = session.req();

					auto res = co_await router_.dispatch(req);

					// if (res.status_code() == 101)
					// {
					// 	std::string client_key =
					// 		req->header("sec-websocket-key");
					// 	std::string accept_key =
					// 		ws::WSHandshake::generate_accept_key(client_key);

					// 	res.header("Upgrade", "websocket")
					// 		.header("Connection", "Upgrade")
					// 		.header("Sec-WebSocket-Accept", accept_key);

					// 	co_await HttpSender::send(&s, &res);

					// 	auto ws_handler = router_.get_ws_handler(req->path);
					// 	if (ws_handler)
					// 	{
					// 		ws::WSConnection ws_conn(&s);
					// 		co_await ws_handler(&ws_conn); // 进入长连接处理循环
					// 	}
					// 	co_return;
					// }

					bool is_keep = (req.header("connection") != "close");
					if (req.version == "HTTP/1.0" &&
						req.header("connection") != "keep-alive")
					{
						is_keep = false;
					}
					res.keep_alive(is_keep);

					co_await HttpSender::send(&s, &res);

					if (!is_keep)
					{
						s.shutdown();
						co_return;
					}
					session.clear();
				}
				else
				{
					break;
				}
			}
		}
	}
	catch (const std::exception& e)
	{
		elog::LOG_ERROR("Exception in handleClient: {}", e.what());
	}

	co_return;
}
} // namespace http
} // namespace netx