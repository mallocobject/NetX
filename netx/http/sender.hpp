#ifndef NETX_HTTP_SENDER_HPP
#define NETX_HTTP_SENDER_HPP

#include "netx/async/task.hpp"
#include "netx/http/response.hpp"
#include "netx/net/stream.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
namespace netx
{
namespace http
{
namespace async = netx::async;
namespace net = netx::net;
class HttpSender
{
  public:
	static async::Task<> send(net::Stream* stream, HttpResponse* res)
	{
		if (res->type() == ResponseType::kFile)
		{
			co_await send_file(stream, res);
		}
		else
		{
			co_await stream->write(res->to_formatted_string());
		}
	}

	HttpSender() = default;
	HttpSender(HttpSender&&) = delete;
	~HttpSender() = default;

  private:
	static async::Task<> send_file(net::Stream* stream, HttpResponse* res)
	{
		const std::string& path = res->get_file();
		struct stat st;

		if (::stat(path.c_str(), &st) == -1 || !S_ISREG(st.st_mode))
		{
			res->status(404)
				.content_type("text/html; charset=utf-8")
				.body("<h1>404 Not Found</h1>");
			co_await stream->write(res->to_formatted_string());
			co_return;
		}

		int fd = ::open(path.c_str(), O_RDONLY);

		if (fd == -1)
		{
			res->status(500)
				.content_type("text/html; charset=utf-8")
				.body("<h1>500 Internal Server Error</h1>");
			co_await stream->write(res->to_formatted_string());
			co_return;
		}

		if (::fstat(fd, &st) == -1 || !S_ISREG(st.st_mode))
		{
			net::Socket::close(fd);
			res->status(500)
				.content_type("text/html; charset=utf-8")
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
				.content_type("text/html; charset=utf-8")
				.body("<h1>500 Internal Server Error</h1>");
			co_await stream->write(res->to_formatted_string());
			co_return;
		}

		res->status(200).header("Content-Length", std::to_string(st.st_size));
		co_await stream->write(res->to_formatted_string());

		char* ptr = reinterpret_cast<char*>(mapped);
		std::size_t remaining = st.st_size;
		while (remaining > 0)
		{
			std::size_t to_send = std::min(remaining, net::Stream::kChunkSize);
			co_await stream->write(std::string_view(ptr, to_send));
			ptr += to_send;
			remaining -= to_send;
		}

		::munmap(mapped, st.st_size);
		net::Socket::close(fd);
		co_return;
	}
};
} // namespace http
} // namespace netx

#endif