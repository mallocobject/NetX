#pragma once

#include "elog/logger.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/http/response.hpp"
#include "netx/net/stream.hpp"
#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>
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
		static std::unordered_map<std::string,
								  std::pair<std::shared_ptr<void>, size_t>>
			file_set;
		static std::shared_mutex file_set_mutex;

		const std::string& file_path = res.file;

		{
			std::shared_lock lock(file_set_mutex);
			if (auto it = file_set.find(file_path); it != file_set.end())
			{
				auto [ptr_holder, size] = it->second;
				lock.unlock();
				co_await co_await mmap_write(stream, res, ptr_holder, size);
				co_return {};
			}
		}

		int fd = ::open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
		if (fd == -1)
		{
			elog::LOG_DEBUG("send_file: not found {}", file_path);
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

		void* mapped = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
		::close(fd);
		if (mapped == MAP_FAILED)
		{
			elog::LOG_ERROR("send_file: mmap failed {}", file_path);
			co_return core::details::make_error_code(
				core::details::Error::ResourceExhausted);
		}

		{
			std::unique_lock<std::shared_mutex> lock(file_set_mutex);
			if (!file_set.contains(file_path))
			{
				file_set.try_emplace(file_path,
									 std::make_pair(std::shared_ptr<void>(
														mapped, [size](void* p)
														{ ::munmap(p, size); }),
													size));
			}
			else
			{
				::munmap(mapped, size);
			}
		}

		co_await co_await send_file(stream, res);
		co_return {};
	}

	static core::Task<core::Expected<>> mmap_write(
		net::details::Stream& stream, Response& res,
		std::shared_ptr<void> ptr_holder, size_t total_size)
	{
		const char* ptr = reinterpret_cast<const char*>(ptr_holder.get());
		size_t remaining = total_size;

		res.with_status(200).with_header("Content-Length",
										 std::to_string(remaining));

		if (auto exp = co_await stream.write(res.to_formatted_string()); !exp)
		{
			co_return exp.error();
		}

		while (remaining > 0)
		{
			size_t to_send =
				std::min(remaining, net::details::Stream::kChunkSize);
			co_await co_await stream.write(std::string_view(ptr, to_send));

			ptr += to_send;
			remaining -= to_send;
		}

		co_return {};
	}
};
} // namespace details
} // namespace http
} // namespace netx