#ifndef ELOG_BUFFER_HPP
#define ELOG_BUFFER_HPP

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>
namespace elog
{
namespace details
{
struct LogBlock
{
	constexpr static size_t kCap = 64 * 1024;

	bool append(std::string_view s)
	{
		if (len + s.size() > kCap)
		{
			return false;
		}
		memcpy(data + len, s.data(), s.size());
		len += s.size();
		return true;
	}

	void clear() noexcept
	{
		len = 0;
	}

	bool empty() const noexcept
	{
		return len == 0;
	}

	size_t len{0};
	char data[kCap];
};

template <size_t N> struct Buffer
{
	using iterator = LogBlock*;
	using const_iterator = const LogBlock*;

	Buffer() : data_(std::make_unique<LogBlock[]>(N))
	{
	}

	Buffer(Buffer&& other)
		: data_(std::exchange(other.data_, nullptr)),
		  idx_(std::exchange(other.idx_, 0))
	{
	}

	Buffer& operator=(Buffer&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		data_ = std::exchange(other.data_, nullptr);
		idx_ = std::exchange(other.idx_, 0);
		return *this;
	}

	size_t size() const noexcept
	{
		return idx_;
	}

	bool check() const noexcept
	{
		return data_ != nullptr;
	}

	size_t capacity() const noexcept
	{
		return N;
	}

	bool empty() const noexcept
	{
		return idx_ == 0;
	}

	bool full() const noexcept
	{
		return idx_ == N;
	}

	void clear()
	{
		idx_ = 0;
	}

	void push(std::string_view msg)
	{
		assert(data_);
		data_[idx_++].append(msg);
	}

	iterator begin() noexcept
	{
		return data_.get();
	}
	iterator end() noexcept
	{
		return data_.get() + idx_;
	}

	const_iterator begin() const noexcept
	{
		return data_.get();
	}
	const_iterator end() const noexcept
	{
		return data_.get() + idx_;
	}

  private:
	std::unique_ptr<LogBlock[]> data_;
	size_t idx_{0};
};
} // namespace details
} // namespace elog

#endif