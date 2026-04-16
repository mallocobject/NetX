#ifndef ELOG_LOG_BLOCK_HPP
#define ELOG_LOG_BLOCK_HPP

#include <cassert>
#include <cstddef>
#include <cstring>
#include <string_view>
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
} // namespace details
} // namespace elog

#endif