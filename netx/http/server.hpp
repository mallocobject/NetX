#pragma once

#include "elog/logger.hpp"
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
#include "netx/websocket/handshake.hpp"
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
		requires std::invocable<Handler, Request&> &&
				 std::same_as<std::invoke_result_t<Handler, Request&>,
							  core::Task<core::Expected<Response>>>
	Server& route(const std::string& method, const std::string& path,
				  Handler&& handler)
	{
		router_.route(method, path, std::forward<Handler>(handler));
		return *this;
	}

	template <typename Handler>
		requires std::invocable<Handler> &&
				 std::same_as<std::invoke_result_t<Handler>,
							  core::Task<core::Expected<>>>
	Server& route(const std::string& path, Handler&& handler)
	{
		router_.route(path, std::forward<Handler>(handler));
		return *this;
	}

  private:
	core::Task<core::Expected<>> handle_client(int read_fd, int write_fd);

  private:
	details::Router router_{};
};

inline core::Task<core::Expected<>> Server::handle_client(int read_fd,
														  int write_fd)
{

	elog::LOG_DEBUG("handle_client start fd={}", read_fd);
	auto stream_exp = net::details::Stream::create(read_fd, write_fd);
	if (!stream_exp)
	{
		elog::LOG_DEBUG("handle_client Stream::create failed fd={}", read_fd);
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
			auto var =
				(co_await core::when_any(s.read(), core::sleep(timeout_)))
					.value();

			if (var.index() == 1)
			{
				elog::LOG_DEBUG("Connection idle timeout, closing fd {}",
								s.read_fd);
				// co_await s.write("HTTP/1.1 408 Request Timeout\r\nConnection:
				// " 				 "close\r\n\r\n");
				co_await s.write(Response{}
									 .with_status(408)
									 .keep_alive(false)
									 .to_formatted_string());
				break;
			}

			auto& read_exp = std::get<0>(var);
			if (!read_exp)
			{
				if (read_exp.error() != core::details::Error::BrokenPipe)
				{
					elog::LOG_DEBUG("Client read error: {}",
									read_exp.error().message());
				}
				break;
			}

			bool should_close = false;
			while (s.read_buf.readable_bytes() > 0)
			{
				if (!session.parse(s.read_buf))
				{
					// co_await s.write("HTTP/1.1 400 Bad Request\r\nConnection:
					// " 				 "close\r\n\r\n");
					co_await s.write(Response{}
										 .with_status(400)
										 .keep_alive(false)
										 .to_formatted_string());
					should_close = true;
					break;
				}

				if (session.completed())
				{
					auto& req = session.req();

					auto res_exp = co_await router_.dispatch(req);

					if (!res_exp)
					{
						const std::error_code& ec = res_exp.error();
						elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
						// co_await s.write(
						// 	"HTTP/1.1 500 Internal Server Error\r\nConnection: "
						// 	"close\r\n\r\n");
						co_await s.write(Response{}
											 .with_status(500)
											 .keep_alive(false)
											 .to_formatted_string());
						should_close = true;
						break;
					}

					Response res = std::move(res_exp.value());
					if (res.status_code == 101)
					{
						std::string client_key =
							req.header("sec-websocket-key");
						std::string accept_key = websocket::details::
							WSHandshake::generate_accept_key(client_key);

						res.with_header("Upgrade", "websocket")
							.with_header("Connection", "Upgrade")
							.with_header("Sec-WebSocket-Accept", accept_key);

						if (auto exp = co_await details::Sender::send(s, res);
							!exp)
						{
							const std::error_code& ec = res_exp.error();
							elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
							break;
						}

						auto ws_handler = router_.get_ws_handler(req.url_path);
						if (ws_handler)
						{
							websocket::details::Connection ws_conn(s);
							co_await ws_handler(ws_conn); // 进入长连接处理循环
						}
						co_return {};
					}

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
						should_close = true;
						break;
					}

					session.clear();
					s.read_buf.try_shrink();
				}
				else
				{
					break;
				}
			}
			if (should_close)
			{
				break;
			}
		}
	}
	catch (const std::exception& e)
	{
		elog::LOG_ERROR("Exception in handleClient: {}", e.what());
	}

	s.shutdown();
	elog::LOG_DEBUG("handle_client end fd={}", read_fd);
	co_return {};
}
} // namespace http
} // namespace netx