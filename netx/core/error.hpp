#pragma once

#include "netx/third_party/expected.hpp"
#include <cerrno>
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

namespace netx::core::details
{

/**
 * @brief NetX 核心错误码映射
 */
enum class Error
{
	Success = 0,
	Timeout = 1,	   // 操作超时
	Cancelled,		   // 操作被取消
	BrokenPipe,		   // 对端关闭连接
	ConnectionReset,   // 连接重置
	ConnectionAborted, // 连接中止
	AlreadyStarted,	   // 资源已注册或任务已启动 (EEXIST)
	InvalidOperation,  // 无效操作 (ENOENT, EBADF, EPERM)
	ResourceExhausted, // 系统资源耗尽 (EMFILE, ENFILE, ENOSPC)
};

/**
 * @brief 错误类别定义
 */
inline const std::error_category& error_category()
{
	static const struct : std::error_category
	{
		const char* name() const noexcept override
		{
			return "NetXCoreError";
		}

		std::string message(int ev) const override
		{
			switch (static_cast<Error>(ev))
			{
			case Error::Success:
				return "Success";
			case Error::Timeout:
				return "Operation timed out";
			case Error::Cancelled:
				return "Operation was cancelled";
			case Error::BrokenPipe:
				return "Broken pipe / Remote closed";
			case Error::ConnectionReset:
				return "Connection reset by peer";
			case Error::ConnectionAborted:
				return "Connection aborted";
			case Error::AlreadyStarted:
				return "Task or operation already started (EEXIST)";
			case Error::InvalidOperation:
				return "Invalid operation for current state "
					   "(ENOENT/EBADF/EPERM)";
			case Error::ResourceExhausted:
				return "System resource (FD/Memory/Watch) exhausted";
			default:
				return "Unknown core error";
			}
		}
	} instance;
	return instance;
}

inline std::error_code make_error_code(Error e)
{
	return {static_cast<int>(e), error_category()};
}

/**
 * @brief 将系统 errno 转换为 std::error_code 并记录关键日志
 */
inline std::error_code from_errno(int err)
{
	if (err == 0)
		return {};

	switch (err)
	{
	case ETIMEDOUT:
		return make_error_code(Error::Timeout);

	case ECANCELED:
		return make_error_code(Error::Cancelled);

	case EPIPE:
		return make_error_code(Error::BrokenPipe);

	case ECONNRESET:
		return make_error_code(Error::ConnectionReset);

	case EMFILE:
	case ENFILE:
		return make_error_code(Error::ResourceExhausted);

	case EEXIST:
		return make_error_code(Error::AlreadyStarted);

	case ENOENT:
		return make_error_code(Error::InvalidOperation);

	case ENOSPC:
		return make_error_code(Error::ResourceExhausted);

	case EBADF:
		return make_error_code(Error::InvalidOperation);

	case EPERM:
		return make_error_code(Error::InvalidOperation);

	case ENOBUFS:
	case ENOMEM:
		return make_error_code(Error::ResourceExhausted);

	default:
		std::error_code ec(err, std::system_category());
		return ec;
	}
}

} // namespace netx::core::details

namespace std
{
template <> struct is_error_code_enum<netx::core::details::Error> : true_type
{
};
} // namespace std