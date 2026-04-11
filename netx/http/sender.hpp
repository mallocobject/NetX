#pragma once

#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/http/response.hpp"
#include "netx/net/stream.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
namespace netx
{
namespace http
{
namespace details
{
class Sender
{
  public:
	static core::Task<core::Expected<>> send(net::details::Stream& stream,
											 Response& res)
	{
		if (res.type == ResponseType::kFile)
		{
			co_await co_await send_file(stream, res);
		}
		else if (res.type == ResponseType::kBody)
		{
			co_await co_await stream.write(res.to_formatted_string());
		}

		co_return {};
	}

	// Sender() = default;
	// Sender(Sender&&) = default;
	// ~Sender() = default;

  private:
	static core::Task<core::Expected<>> send_file(net::details::Stream& stream,
												  Response& res)
	{
		const std::string& path = res.file;
		struct stat st;

		if (::stat(path.c_str(), &st) == -1 || !S_ISREG(st.st_mode))
		{
			res.with_status(404)
				.content_type("text/html; charset=utf-8")
				.with_body("<h1>404 Not Found</h1>");
			co_await co_await stream.write(res.to_formatted_string());
		}

		int fd = ::open(path.c_str(), O_RDONLY);

		if (fd == -1)
		{
			res.with_status(500)
				.content_type("text/html; charset=utf-8")
				.with_body("<h1>500 Internal Server Error</h1>");
			co_await co_await stream.write(res.to_formatted_string());
		}

		if (::fstat(fd, &st) == -1 || !S_ISREG(st.st_mode))
		{
			net::details::Socket::close(fd);
			res.with_status(500)
				.content_type("text/html; charset=utf-8")
				.with_body("<h1>500 Internal Server Error</h1>");
			co_await co_await stream.write(res.to_formatted_string());
		}

		void* mapped =
			::mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mapped == MAP_FAILED)
		{
			net::details::Socket::close(fd);
			res.with_status(500)
				.content_type("text/html; charset=utf-8")
				.with_body("<h1>500 Internal Server Error</h1>");
			co_return co_await stream.write(res.to_formatted_string());
		}

		res.with_status(200).with_header("Content-Length",
										 std::to_string(st.st_size));
		co_return co_await stream.write(res.to_formatted_string());

		char* ptr = reinterpret_cast<char*>(mapped);
		std::size_t remaining = st.st_size;
		while (remaining > 0)
		{
			std::size_t to_send =
				std::min(remaining, net::details::Stream::kChunkSize);
			co_await co_await stream.write(std::string_view(ptr, to_send));

			ptr += to_send;
			remaining -= to_send;
		}

		::munmap(mapped, st.st_size);
		net::details::Socket::close(fd);
		co_return true;
	}
};
} // namespace details
} // namespace http
} // namespace netx