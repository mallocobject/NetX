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
			co_return co_await send_file(stream, res);
		}
		else
		{
			co_return co_await stream.write(res.to_formatted_string());
		}
	}

	// Sender() = default;
	// Sender(Sender&&) = default;
	// ~Sender() = default;

  private:
	static core::Task<core::Expected<>> send_file(net::details::Stream& stream,
												  Response& res)
	{
		const std::string& path = res.file;
		int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (fd == -1)
		{
			res.with_status(404).with_body("<h1>404 Not Found</h1>");
			co_return co_await stream.write(res.to_formatted_string());
		}

		struct stat st;
		if (::fstat(fd, &st) == -1 || !S_ISREG(st.st_mode))
		{
			::close(fd);
			res.with_status(404).with_body("<h1>404 Not Found</h1>");
			co_return co_await stream.write(res.to_formatted_string());
		}

		size_t size = st.st_size;
		res.with_status(200).with_header("Content-Length",
										 std::to_string(size));

		if (auto exp = co_await stream.write(res.to_formatted_string()); !exp)
		{
			::close(fd);
			co_return exp.error();
		}

		if (size == 0)
		{
			::close(fd);
			co_return {};
		}

		void* mapped = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
		::close(fd);
		if (mapped == MAP_FAILED)
		{
			co_return core::details::make_error_code(
				core::details::Error::ResourceExhausted);
		}

		const char* ptr = reinterpret_cast<const char*>(mapped);
		size_t remaining = size;
		core::Expected<> write_res{};

		while (remaining > 0)
		{
			size_t to_send =
				std::min(remaining, net::details::Stream::kChunkSize);
			write_res = co_await stream.write(std::string_view(ptr, to_send));

			if (!write_res)
			{
				break;
			}

			ptr += to_send;
			remaining -= to_send;
		}

		::munmap(mapped, st.st_size);
		co_return write_res;
	}
};
} // namespace details
} // namespace http
} // namespace netx