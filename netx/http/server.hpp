#ifndef NETX_HTTP_SERVER_HPP
#define NETX_HTTP_SERVER_HPP

#include "netx/http/router.hpp"
#include "netx/http/session.hpp"
#include "netx/net/server.hpp"
#include "netx/net/stream.hpp"
#include "netx/async/sleep.hpp"
#include "netx/async/when_any.hpp"
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
    static async::Task<> graceful_close(
		net::Stream& s,
		std::chrono::milliseconds drain_timeout = std::chrono::milliseconds(10));
	async::Task<> handleClient(int read_fd, int write_fd);

	// async::Task<> serverLoop();

  private:
	HttpRouter router_{};
};

inline async::Task<> HttpServer::graceful_close(
    net::Stream& s,
    std::chrono::milliseconds drain_timeout)
{
    s.shutdown();
    s.read_buffer()->retrieve(s.read_buffer()->readableBytes());

    while (true)
    {
        auto ret =
            co_await async::when_any(s.read(), async::sleep(drain_timeout));

        if (ret.index() == 1 || !std::get<0>(ret))
        {
            break;
        }

        s.read_buffer()->retrieve(s.read_buffer()->readableBytes());
    }
}

inline async::Task<> HttpServer::handleClient(int read_fd, int write_fd)
{
    net::Stream s{read_fd, write_fd};
    Session session{};

    try {
        while (true)
        {
            auto ret = co_await async::when_any(s.read(), async::sleep(timeout_));

            if (ret.index() == 1) 
            {
				elog::LOG_WARN("Connection timeout on fd: {}", read_fd);
                co_await s.write(
                    "HTTP/1.1 408 Request Timeout\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                );
                co_await graceful_close(s);
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
                    co_await s.write(
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                    );
					
                    co_await graceful_close(s);
					co_return;
				}

                if (session.completed())
                {
                    const http::HttpRequest& req = session.req();
                    http::HttpResponse res;

                    co_await router_.dispatch(req, &res, &s);

                    std::string conn_header = req.header("connection");
                    if (conn_header == "close" ||
                        (req.version == "HTTP/1.0" && conn_header != "keep-alive"))
                    {
                        co_await graceful_close(s);
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