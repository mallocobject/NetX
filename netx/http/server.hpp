#pragma once

#include "netx/core/error.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/sleep.hpp"
#include "netx/core/when_any.hpp"
#include "netx/http/router.hpp"
#include "netx/http/sender.hpp"
#include "netx/http/session.hpp"
#include "netx/net/server.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"
#include <cassert>
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
		// FIXME
		int fd1 = open("/dev/null", O_RDONLY | O_CLOEXEC);
		int fd2 = (fd1 >= 0) ? dup(fd1) : -1;
		assert(fd1 != -1 && fd2 != -1);
		int fd = net::details::Socket::socket().value();
		static Server http_server{net::details::Stream::create(fd).value(), fd1,
								  fd2};
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

	auto stream_exp = net::details::Stream::create(read_fd, write_fd);
	if (!stream_exp.has_value())
	{
		::close(read_fd);
		::close(write_fd);
		co_return {};
	}

	net::details::Stream s = std::move(stream_exp.value());
	details::Session session{};

	try
	{
		while (true)
		{
			if (s.write_fd == -1)
			{
				co_return {};
			}

			auto any_res =
				co_await core::when_any(s.read(), core::sleep(timeout_));

			if (!any_res.has_value())
			{
				co_return {};
			}

			auto& val = any_res.value();
			if (val.index() == 1)
			{
				co_await s.write("HTTP/1.1 408 Request Timeout\r\nConnection: "
								 "close\r\n\r\n");
				s.shutdown();
				co_return {};
			}

			auto& read_exp = std::get<0>(val);
			if (!read_exp)
			{
				if (read_exp.error() != core::details::Error::BrokenPipe)
				{
					elog::LOG_DEBUG("Client read error: {}",
									read_exp.error().message());
				}
				co_return {};
			}

			while (s.read_buf.readable_bytes() > 0)
			{
				if (!session.parse(s.read_buf))
				{
					co_await s.write("HTTP/1.1 400 Bad Request\r\nConnection: "
									 "close\r\n\r\n");
					s.shutdown();
					co_return {};
				}

				if (session.completed())
				{
					auto& req = session.req();

					auto res_exp = co_await router_.dispatch(req);

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

					Response res = res_exp ? std::move(res_exp.value())
										   : Response{}.with_status(500);

					bool is_keep = (req.header("connection") != "close");
					if (req.version == "HTTP/1.0" &&
						req.header("connection") != "keep-alive")
					{
						is_keep = false;
					}
					res.keep_alive(is_keep);

					auto send_res = co_await details::Sender::send(s, res);
					if (!send_res || !is_keep)
					{
						s.shutdown();
						co_return {};
					}

					session.clear();
					s.read_buf.try_shrink();
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

	co_return {};
}
} // namespace http
} // namespace netx