#ifndef NETX_NET_BUFFER_HPP
#define NETX_NET_BUFFER_HPP

#include "netx/meta/endian_helper.hpp"
#include "netx/rpc/header.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <endian.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/uio.h>
#include <utility>
#include <vector>

namespace netx
{
namespace net
{
class Buffer;
}
namespace rpc
{
template <typename Tuple, std::size_t... Is>
void serialize_tuple_tlv_impl(net::Buffer* buf, const Tuple& t,
							  std::index_sequence<Is...>);
}
} // namespace netx

namespace netx
{
namespace net
{
namespace meta = netx::meta;
namespace rpc = netx::rpc;
class Buffer
{
	template <typename Tuple, std::size_t... Is>
	friend void rpc::serialize_tuple_tlv_impl(Buffer* buf, const Tuple& t,
											  std::index_sequence<Is...>);

	inline static constexpr std::size_t kMaxBufferSize = 16 * 1024;
	inline static constexpr std::size_t kInitBufferSize = 1024;
	inline static constexpr std::size_t kPrependSize = 32; // enough
	inline static constexpr char kCRLF[] = "\r\n";

	static_assert(sizeof(rpc::RpcHeaderWire) <= kPrependSize,
				  "insufficient preallocated space");

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

	template <typename T> void appendInt(T x)
	{
		static_assert(std::is_integral_v<T>,
					  "appendInt<T>: T must be integral");
		T be = meta::hostToBE(x);
		append(reinterpret_cast<const char*>(&be), sizeof(be));
	}

	template <typename T> void prependInt(T x)
	{
		static_assert(std::is_integral_v<T>,
					  "prependInt<T>: T must be integral");
		T be = meta::hostToBE(x);
		prepend(&be, sizeof(be));
	}

	template <typename T> T peekInt() const
	{
		static_assert(std::is_integral_v<T>, "peekInt<T>: T must be integral");
		assert(readableBytes() >= sizeof(T));
		T be{};
		memcpy(&be, peek(), sizeof(be));
		return meta::beToHost(be);
	}

	template <typename T> T retrieveInt()
	{
		T v = peekInt<T>();
		retrieve(sizeof(T));
		return v;
	}

	void appendRpcHeader(const rpc::RpcHeader& h)
	{
		rpc::RpcHeaderWire be_h = rpc::to_wire(h);
		append(reinterpret_cast<const char*>(&be_h),
			   sizeof(rpc::RpcHeaderWire));
	}

	void prependRpcHeader(const rpc::RpcHeader& h)
	{
		rpc::RpcHeaderWire be_h = to_wire(h);
		prepend(&be_h, sizeof(rpc::RpcHeaderWire));
	}

	rpc::RpcHeader peekRpcHeader() const
	{
		assert(readableBytes() >= sizeof(rpc::RpcHeaderWire));
		rpc::RpcHeaderWire h{};
		memcpy(&h, peek(), sizeof(rpc::RpcHeaderWire));
		return rpc::to_host(h);
	}

	rpc::RpcHeader retrieveRpcHeader()
	{
		rpc::RpcHeader v = peekRpcHeader();
		retrieve(sizeof(rpc::RpcHeaderWire));
		return v;
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

	// void move(std::uint32_t len)
	// {
	// 	std::size_t n = sizeof(len);
	// 	ensureWritableBytes(n);
	// 	std::copy(data_.data() + wptr_ - len, data_.data() + wptr_,
	// 			  data_.data() + rptr_ - len + n);
	// 	wptr_ += n;
	// }

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
} // namespace net
} // namespace netx

#endif