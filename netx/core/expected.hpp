#pragma once

#include <cerrno>
#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>

namespace netx::core::details {

/**
 * @brief NetX 核心错误码映射
 */
enum class Error {
    Success = 0,
    Timeout = 1,       // 操作超时
    Cancelled,         // 操作被取消
    BrokenPipe,        // 对端关闭连接
    ConnectionReset,   // 连接重置
    ConnectionAborted, // 连接中止
    AlreadyStarted,    // 资源已注册或任务已启动 (EEXIST)
    InvalidOperation,  // 无效操作 (ENOENT, EBADF, EPERM)
    ResourceExhausted, // 系统资源耗尽 (EMFILE, ENFILE, ENOSPC)
};

/**
 * @brief 错误类别定义
 */
inline const std::error_category &error_category() {
    static const struct : std::error_category {
        const char *name() const noexcept override {
            return "NetXCoreError";
        }

        std::string message(int ev) const override {
            switch (static_cast<Error>(ev)) {
                using enum Error;
            case Success:
                return "Success";
            case Timeout:
                return "Operation timed out";
            case Cancelled:
                return "Operation was cancelled";
            case BrokenPipe:
                return "Broken pipe / Remote closed";
            case ConnectionReset:
                return "Connection reset by peer";
            case ConnectionAborted:
                return "Connection aborted";
            case AlreadyStarted:
                return "Task or operation already started (EEXIST)";
            case InvalidOperation:
                return "Invalid operation for current state "
                       "(ENOENT/EBADF/EPERM)";
            case ResourceExhausted:
                return "System resource (FD/Memory/Watch) exhausted";
            default:
                return "Unknown core error";
            }
        }
    } instance;
    return instance;
}

inline std::error_code make_error_code(Error e) {
    return {static_cast<int>(e), error_category()};
}

/**
 * @brief 将系统 errno 转换为 std::error_code
 *
 * 已映射的 errno 走本库的错误类别（NetXCoreError）；未映射的保留
 * std::system_category()，调用方仍能拿到原始系统错误信息。
 */
inline std::error_code from_errno(int err) {
    if (err == 0)
        return {};

    switch (err) {
        using enum Error;
    case ETIMEDOUT:
        return make_error_code(Timeout);

    case ECANCELED:
        return make_error_code(Cancelled);

    case EPIPE:
        return make_error_code(BrokenPipe);

    case ECONNRESET:
        return make_error_code(ConnectionReset);

    case EMFILE:
    case ENFILE:
        return make_error_code(ResourceExhausted);

    case EEXIST:
        return make_error_code(AlreadyStarted);

    case ENOENT:
        return make_error_code(InvalidOperation);

    case ENOSPC:
        return make_error_code(ResourceExhausted);

    case EBADF:
        return make_error_code(InvalidOperation);

    case EPERM:
        return make_error_code(InvalidOperation);

    case ENOBUFS:
    case ENOMEM:
        return make_error_code(ResourceExhausted);

    default:
        std::error_code ec(err, std::system_category());
        return ec;
    }
}

} // namespace netx::core::details

namespace netx::core {
/**
 * @brief 本库统一的 expected 返回类型
 *
 * T 默认 void，表示"只关心成功/失败、不携带值"。std::expected<void, E>
 * 本身就由标准单独处理（没有 union 存储、operator* 返回 void），所以
 * void 情形无需也无法用特化表达 —— 别名模板不允许特化。
 */
template <typename T = void>
using Expected = std::expected<T, std::error_code>;
} // namespace netx::core

namespace netx::core::details {
/**
 * @brief 从 Expected 取值，失败时抛 std::system_error
 *
 * std::expected::value() 只会抛 std::bad_expected_access<std::error_code>，
 * 且该异常类型是标准库的一部分、不允许定制，所以这里提供与本库
 * check_error.hpp 一致的 std::system_error 语义。
 *
 * 注意 std::expected 要求 T 是非引用对象类型（引用需自行包成
 * std::reference_wrapper）。
 */
template <typename T>
inline T value_or_throw(Expected<T> result) {
    if (!result) {
        throw std::system_error(result.error());
    }
    if constexpr (std::is_void_v<T>) {
        return;
    } else {
        return std::move(*result);
    }
}

inline auto from_errno_to_unexpected(int err) {
    return std::unexpected<std::error_code>{from_errno(err)};
}

} // namespace netx::core::details

namespace std {
template <>
struct is_error_code_enum<netx::core::details::Error> : true_type {};
} // namespace std