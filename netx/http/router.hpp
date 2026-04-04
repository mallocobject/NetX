#ifndef NETX_HTTP_ROUTER_HPP
#define NETX_HTTP_ROUTER_HPP

#include "netx/async/task.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/meta/radix_tree.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"
#include <cassert>
#include <fcntl.h>
#include <functional>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>
namespace netx
{
namespace http
{
namespace async = netx::async;
namespace net = netx::net;
namespace meta = netx::meta;
using TaskType = async::Task<>;
using HttpHandler =
	std::function<TaskType(const HttpRequest&, HttpResponse*, net::Stream*)>;
class HttpRouter
{
  public:
	template <typename Handler>
	void route(const std::string& method, const std::string& path,
			   Handler&& handler)
	{
		trees_[method].insert(path, std::forward<Handler>(handler));
	}

	TaskType dispatch(const HttpRequest& req, HttpResponse* res,
					  net::Stream* stream);

	static TaskType send_file(net::Stream* stream, HttpResponse* res,
							  const std::string& file_path);

	HttpRouter() = default;
	HttpRouter(HttpRouter&&) = delete;
	~HttpRouter() = default;

  private:
	static std::string get_mine_type(const std::string& path);

  private:
	std::unordered_map<std::string, meta::RadixTree<HttpHandler>> trees_;
};

inline TaskType HttpRouter::dispatch(const HttpRequest& req, HttpResponse* res,
									 net::Stream* stream)
{
	HttpHandler f;
	std::string method = req.method;
	std::string path = req.path;

	if ((f = trees_[method].search(path)))
	{
		co_await f(req, res, stream);
	}
	else
	{
		res->status(404)
			.content_type("text/html")
			.body("<h1>404 Not Found</h1>");

		co_await stream->write(res->to_formatted_string());
	}
}

inline TaskType HttpRouter::send_file(net::Stream* stream, HttpResponse* res,
									  const std::string& file_path)
{
	struct stat st;
	if (::stat(file_path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
	{
		int fd = ::open(file_path.c_str(), O_RDONLY);
		if (fd == -1)
		{
			res->status(500)
				.content_type("text/html")
				.body("<h1>500 Internal Server Error</h1>");
			co_await stream->write(res->to_formatted_string());
			co_return;
		}

		if (::fstat(fd, &st) == -1 || !S_ISREG(st.st_mode))
		{
			net::Socket::close(fd);
			res->status(500)
				.content_type("text/html")
				.body("<h1>500 Internal Server Error</h1>");
			co_await stream->write(res->to_formatted_string());
			co_return;
		}

		void* mapped =
			::mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mapped == MAP_FAILED)
		{
			net::Socket::close(fd);
			res->status(500)
				.content_type("text/html")
				.body("<h1>500 Internal Server Error</h1>");
			co_await stream->write(res->to_formatted_string());
			co_return;
		}

		res->status(200)
			.content_type(get_mine_type(file_path))
			.header("Content-Length", std::to_string(st.st_size));
		co_await stream->write(res->to_formatted_string());

		const std::size_t chunk_size = net::Stream::kChunkSize;
		char* ptr = static_cast<char*>(mapped);
		std::size_t remaining = st.st_size;
		while (remaining > 0)
		{
			std::size_t to_send = std::min(remaining, chunk_size);
			co_await stream->write(std::string_view(ptr, to_send));
			ptr += to_send;
			remaining -= to_send;
		}

		::munmap(mapped, st.st_size);
		net::Socket::close(fd);
		co_return;
	}
	res->status(404).content_type("text/html").body("<h1>404 Not Found</h1>");
	co_await stream->write(res->to_formatted_string());
}

inline std::string HttpRouter::get_mine_type(const std::string& path)
{
	if (path.ends_with(".html"))
	{
		return "text/html; charset=utf-8";
	}
	else if (path.ends_with(".css"))
	{
		return "text/css";
	}
	else if (path.ends_with(".js"))
	{
		return "application/javascript";
	}
	else if (path.ends_with(".png"))
	{
		return "image/png";
	}
	else if (path.ends_with(".jpg") || path.ends_with(".jpeg"))
	{
		return "image/jpeg";
	}

	return "application/octet-stream";
}
} // namespace http
} // namespace netx

#endif