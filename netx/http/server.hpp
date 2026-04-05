#ifndef NETX_HTTP_SERVER_HPP
#define NETX_HTTP_SERVER_HPP

#include "elog/logger.hpp"
#include "netx/async/sleep.hpp"
#include "netx/async/when_any.hpp"
#include "netx/http/router.hpp"
#include "netx/http/sender.hpp"
#include "netx/http/session.hpp"
#include "netx/net/server.hpp"
#include "netx/net/stream.hpp"
#include <chrono>
namespace netx
{
namespace http
{
namespace async = netx::async;
namespace net = netx::net;
class HttpServer : public net::Server<HttpServer>
{
	friend class net::Server<HttpServer>;
	HttpServer() = default;

  public:
	static HttpServer& server()
	{
		static HttpServer http_server{};
		return http_server;
	}

	template <typename Handler>
	HttpServer& route(const std::string& method, const std::string& path,
					  Handler&& handler)
	{
		router_.route(method, path, std::forward<Handler>(handler));
		return *this;
	}

  private:
	async::Task<> handleClient(int read_fd, int write_fd);

  private:
	HttpRouter router_{};
};

inline async::Task<> HttpServer::handleClient(int read_fd, int write_fd)
{

	net::Stream s{read_fd, write_fd};
	Session session{};

	try
	{
		while (true)
		{
			auto ret =
				co_await async::when_any(s.read(), async::sleep(timeout_));

			if (s.write_fd() == -1)
			{
				co_return;
			}

			if (ret.index() == 1)
			{

				// elog::LOG_WARN("Connection timeout on fd: {}", read_fd);
				co_await s.write("HTTP/1.1 408 Request Timeout\r\n"
								 "Content-Length: 0\r\n"
								 "Connection: close\r\n"
								 "\r\n");

				s.shutdown();
				co_return;
			}

			if (!std::get<0>(ret))
			{
				co_return;
			}

			while (s.read_buffer()->readableBytes() > 0)
			{
				if (!session.parse(s.read_buffer()))
				{
					// elog::LOG_FATAL("{}", s.write_fd());
					co_await s.write("HTTP/1.1 400 Bad Request\r\n"
									 "Content-Length: 0\r\n"
									 "Connection: close\r\n"
									 "\r\n");
					s.shutdown();
					co_return;
				}

				if (session.completed())
				{
					auto req = session.req();

					http::HttpResponse res = co_await router_.dispatch(req);

					bool is_keep = (req->header("connection") != "close");
					if (req->version == "HTTP/1.0" &&
						req->header("connection") != "keep-alive")
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

#endif