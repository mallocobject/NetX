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

namespace netx
{
namespace core
{
namespace details
{
enum class Error
{
	Timeout,
};

inline const std::error_category& error_category()
{
	static const struct : std::error_category
	{
		const char* name() const noexcept override
		{
			return "CoreError";
		}

		std::string message(int ev) const override
		{
			switch (static_cast<Error>(ev))
			{
			case Error::Timeout:
				return "Operation timed out";
			default:
				return "Unknown error";
			}
		}
	} instance;
	return instance;
}

inline std::error_code make_error_code(Error e)
{
	return {static_cast<int>(e), error_category()};
}

} // namespace details
} // namespace core
} // namespace netx

namespace std
{
template <> struct is_error_code_enum<netx::core::details::Error> : true_type
{
};
} // namespace std