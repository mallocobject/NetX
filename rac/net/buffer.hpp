#ifndef RAC_NET_BUFFER_HPP
#define RAC_NET_BUFFER_HPP

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <endian.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/uio.h>
#include <utility>
#include <vector>
namespace rac
{
class Buffer
{
	inline static constexpr std::size_t kMaxBufferSize = 16 * 1024;
	inline static constexpr std::size_t kInitBufferSize = 1024;
	inline static constexpr std::size_t kPrependSize = 8;
	inline static constexpr char kCRLF[] = "\r\n";

  public:
	Buffer() = default;
	~Buffer() = default;
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

	void swap(Buffer& rhs) noexcept
	{
		data_.swap(rhs.data_);
		std::swap(rptr_, rhs.rptr_);
		std::swap(wptr_, rhs.wptr_);
	}

	std::size_t readableBytes() const
	{
		assert(wptr_ >= rptr_);
		return wptr_ - rptr_;
	}

	std::size_t writableBytes() const
	{
		assert(data_.size() >= wptr_);
		return data_.size() - wptr_;
	}

	std::size_t prependableBytes() const noexcept
	{
		return rptr_;
	}

	const char* peek() const noexcept
	{
		return data_.data() + rptr_;
	}

	void retrieve(std::size_t len)
	{
		assert(len <= readableBytes());
		if (len < readableBytes())
		{
			rptr_ += len;
		}
		else
		{
			retrieveAll();
		}
	}

	void append(const char* data, std::size_t len)
	{
		ensureWritableBytes(len);
		std::copy(data, data + len, data_.data() + wptr_);
		wptr_ += len;
	}

	void append(const std::string& str)
	{
		return append(str.data(), str.size());
	}

	void append(std::string_view strv)
	{
		return append(strv.data(), strv.size());
	}

	void prepend(const void* data, std::size_t len)
	{
		assert(len <= prependableBytes());
		rptr_ -= len;
		const char* d = reinterpret_cast<const char*>(data);
		std::copy(d, d + len, data_.data() + rptr_);
	}

	void shrink(std::size_t reserve)
	{
		Buffer other;
		other.ensureWritableBytes(readableBytes() + reserve);
		other.append(peek(), readableBytes());
		swap(other);
	}

	void try_shrink()
	{
		if (readableBytes() == 0 && data_.size() > kMaxBufferSize)
		{
			shrink(0);
		}
	}

	void appendInt32(std::int32_t x)
	{
		std::int32_t be32 = htonl(x);
		append(reinterpret_cast<const char*>(&be32), sizeof(be32));
	}

	void prependInt32(std::int32_t x)
	{
		std::int32_t be32 = ::htonl(x);
		prepend(&be32, sizeof(be32));
	}

	std::int32_t peekInt32() const
	{
		assert(readableBytes() >= sizeof(int32_t));
		std::int32_t be32 = 0;
		memcpy(&be32, peek(), sizeof(be32));
		return ntohl(be32);
	}

	std::int32_t retrieveInt32()
	{
		int32_t result = peekInt32();
		retrieve(sizeof(std::int32_t));
		return result;
	}

	std::string retrieve_string(std::size_t len)
	{
		assert(len <= readableBytes());
		std::string result(peek(), len);
		retrieve(len);
		return result;
	}

	const char* find_CRLF(const char* start = nullptr) const;

	ssize_t read_fd(int fd, int* saved_errno);

  private:
	void retrieveAll() noexcept
	{
		rptr_ = wptr_ = kPrependSize;
	}

	void ensureWritableBytes(std::size_t len)
	{
		if (len > writableBytes())
		{
			makeSpace(len);
		}
	}

	void makeSpace(std::size_t len);

  private:
	std::vector<char> data_ = std::vector<char>(kInitBufferSize, 0);
	std::size_t rptr_{kPrependSize};
	std::size_t wptr_{kPrependSize};
	// bool read_enable_{true};
};

inline const char* Buffer::find_CRLF(const char* start) const
{
	const char* begin = data_.data() + rptr_;
	const char* end = data_.data() + wptr_;
	if (start != nullptr && (start < begin || start >= end))
	{
		return nullptr;
	}

	const char* crlf =
		std::search(start == nullptr ? begin : start, end, kCRLF, kCRLF + 2);
	return crlf == end ? nullptr : crlf;
}

inline ssize_t Buffer::read_fd(int fd, int* saved_errno)
{
	thread_local char extra_buf[65536]; // 64KB
	thread_local iovec vec[2];

	const size_t writable_bytes = writableBytes();

	vec[0].iov_base = data_.data() + wptr_;
	vec[0].iov_len = writable_bytes;

	vec[1].iov_base = extra_buf;
	vec[1].iov_len = sizeof(extra_buf);

	const ssize_t n = readv(fd, vec, 2); // LT 触发

	if (n < 0)
	{
		*saved_errno = errno; // move to upper struct
	}
	else if (static_cast<size_t>(n) <= writable_bytes)
	{
		wptr_ += n;
	}
	else
	{
		// buffer is full
		wptr_ = data_.size();
		append(extra_buf, n - writable_bytes);
	}
	return n;
}

inline void Buffer::makeSpace(std::size_t len)
{
	// 可写和已读空间之和不够 len
	// writableBytes + prependableBytes - kPrependSize < len
	if (writableBytes() + prependableBytes() < len + kPrependSize)
	{
		data_.resize(wptr_ + len);
	}
	else
	{
		size_t readabel_bytes = readableBytes();
		std::copy(data_.data() + rptr_, data_.data() + wptr_,
				  data_.data() + kPrependSize);

		rptr_ = kPrependSize;
		wptr_ = rptr_ + readabel_bytes;
	}
}
} // namespace rac

#endif