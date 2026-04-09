#pragma once

#include <cerrno>
#include <system_error>
#if !defined(NDEBUG)
#include <source_location>
#endif
namespace netx
{
namespace core
{
#if !defined(NDEBUG)
template <int... BlockErrs>
auto check_error(
	auto res, std::source_location const& loc = std::source_location::current())
{
	if (res == -1)
	{
		const bool acceptable = ((errno == BlockErrs) || ...);
		if (!acceptable) [[unlikely]]
		{
			throw std::system_error(errno, std::system_category(),
									(std::string)loc.file_name() + ":" +
										std::to_string(loc.line()));
		}
		res = errno;
	}
	return res;
}

#else
template <int... BlockErrs> auto check_error(auto res)
{
	if (res == -1)
	{
		const bool acceptable = ((errno == BlockErrs) || ...);
		if (!acceptable) [[unlikely]]
		{
			throw std::system_error(errno, std::system_category());
		}
		res = errno;
	}
	return res;
}
#endif
} // namespace core
} // namespace netx