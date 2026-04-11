#pragma once

#include "netx/core/error.hpp"
#include "netx/core/expected.hpp"
#include "netx/net/endian.hpp"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/uio.h>
#include <type_traits>
#include <utility>
#include <vector>
namespace netx
{
namespace net
{
namespace details
{
struct Buffer
{
	inline static constexpr size_t kMaxBufferSize = 16 * 1024;
	inline static constexpr size_t kInitBufferSize = 1024;
	inline static constexpr size_t kPrependSize = 32;
	inline static constexpr char CRLF[] = "\r\n";

	Buffer() : data_(kInitBufferSize)
	{
	}

	Buffer(Buffer&& other) noexcept
		: data_(std::move(other.data_)), rptr_(std::exchange(other.rptr_, 0)),
		  wptr_(std::exchange(other.wptr_, 0))
	{
	}

	Buffer& operator=(Buffer&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		data_.clear();
		data_ = std::move(other.data_);
		rptr_ = std::exchange(other.rptr_, 0);
		wptr_ = std::exchange(other.wptr_, 0);
		return *this;
	}

	void swap(Buffer& other) noexcept
	{
		data_.swap(other.data_);
		std::swap(rptr_, other.rptr_);
		std::swap(wptr_, other.wptr_);
	}

	size_t readable_bytes() const noexcept
	{
		assert(wptr_ >= rptr_);
		return wptr_ - rptr_;
	}

	size_t writable_bytes() const noexcept
	{
		assert(data_.size() >= rptr_);
		return data_.size() - rptr_;
	}

	size_t prependable_bytes() const noexcept
	{
		return rptr_;
	}

	const char* peek() const noexcept
	{
		return data_.data() + rptr_;
	}

	void retrieve(size_t len)
	{
		assert(len <= readable_bytes());
		if (len < readable_bytes())
		{
			rptr_ += len;
		}
		else
		{
			retrieve_all();
		}
	}

	std::string retrieve_string(size_t len)
	{
		assert(len <= readable_bytes());
		std::string result(peek(), len);
		retrieve(len);
		return result;
	}

	void append(const char* data, size_t len)
	{
		ensure_writable_bytes(len);
		std::copy(data, data + len, data_.data() + wptr_);
		wptr_ += len;
	}

	void append(std::string_view data)
	{
		append(data.data(), data.size());
	}

	void prepend(const void* data, size_t len)
	{
		assert(len <= prependable_bytes());
		rptr_ -= len;
		const char* d = reinterpret_cast<const char*>(data);
		std::copy(d, d + len, data_.data() + rptr_);
	}

	void shrink(size_t extra_size)
	{
		Buffer tmp{};
		tmp.ensure_writable_bytes(readable_bytes() + extra_size);
		tmp.append(peek(), readable_bytes());
		swap(tmp);
	}

	void try_shrink()
	{
		if (readable_bytes() == 0 && data_.size() > kMaxBufferSize)
		{
			shrink(0);
		}
	}

	template <typename T> void append_integer(T x)
	{
		static_assert(std::is_integral_v<T>,
					  "append_integer<T>: T must be integer");
		T be = host2be(x);
		append(reinterpret_cast<const char*>(&be), sizeof(T));
	}

	template <typename T> void prepend_integer(T x)
	{
		static_assert(std::is_integral_v<T>,
					  "prepend_integer<T>: T must be integer");
		T be = host2be(x);
		prepend(reinterpret_cast<const char*>(&be), sizeof(T));
	}

	template <typename T> T peek_integer() const
	{
		static_assert(std::is_integral_v<T>,
					  "peek_integer<T>: T must be integer");
		assert(readable_bytes() > sizeof(T));
		T be;
		memcpy(&be, peek(), sizeof(T));
		return be2host(be);
	}

	template <typename T> T retrieve_integer()
	{
		T v = peek_integer<T>();
		retrieve(sizeof(T));
		return v;
	}

	const char* find_CRLF(const char* start = nullptr) const noexcept;
	core::Expected<ssize_t> read_fd(int fd);

  private:
	void retrieve_all() noexcept
	{
		rptr_ = wptr_ = kPrependSize;
	}

	void ensure_writable_bytes(size_t len)
	{
		if (len > writable_bytes())
		{
			extend_space(len);
		}
	}

	void extend_space(size_t len);

  private:
	std::vector<char> data_;
	size_t rptr_{kPrependSize};
	size_t wptr_{kPrependSize};
};

inline const char* Buffer::find_CRLF(const char* start) const noexcept
{
	const char* begin = data_.data() + rptr_;
	const char* end = data_.data() + wptr_;
	if (start != nullptr && (start < begin || start >= end))
	{
		return nullptr;
	}

	const char* crlf =
		std::search(start == nullptr ? begin : start, end, CRLF, CRLF + 2);
	return crlf == end ? nullptr : crlf;
}

inline core::Expected<ssize_t> Buffer::read_fd(int fd)
{
	thread_local char extra_buf[65536]; // 64KB
	thread_local iovec vec[2];

	const size_t bytes = writable_bytes();

	vec[0].iov_base = data_.data() + wptr_;
	vec[0].iov_len = bytes;

	vec[1].iov_base = extra_buf;
	vec[1].iov_len = sizeof(extra_buf);

	const ssize_t n = readv(fd, vec, 2); // LT 触发

	if (n < 0)
	{
		if (errno == EWOULDBLOCK || errno == EAGAIN)
		{
			return -1;
		}
		return core::details::from_errno(errno);
	}
	else if (n == 0)
	{
		return core::details::make_error_code(core::details::Error::BrokenPipe);
	}

	else
	{
		if (static_cast<size_t>(n) <= bytes)
		{
			wptr_ += n;
		}
		else
		{
			// buffer is full
			wptr_ = data_.size();
			append(extra_buf, n - bytes);
		}
	}
	return n;
}

inline void Buffer::extend_space(size_t len)
{
	// 可写和已读空间之和不够 len
	// writableBytes + prependableBytes - kPrependSize < len
	if (writable_bytes() + prependable_bytes() < len + kPrependSize)
	{
		data_.resize(wptr_ + len);
	}
	else
	{
		size_t readabel_bytes = readable_bytes();
		std::copy(data_.data() + rptr_, data_.data() + wptr_,
				  data_.data() + kPrependSize);

		rptr_ = kPrependSize;
		wptr_ = rptr_ + readabel_bytes;
	}
}
} // namespace details
} // namespace net
} // namespace netx