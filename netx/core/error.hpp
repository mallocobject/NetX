#pragma once

#include "netx/third_party/expected.hpp"
#include <system_error>
namespace tl
{
template <> struct bad_expected_access<std::error_code> : std::system_error
{
	explicit bad_expected_access(std::error_code e) : std::system_error(e)
	{
	}
};
} // namespace tl