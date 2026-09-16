#include "netx/core/expected.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <expected>
#include <iterator>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

using netx::core::Expected;
using netx::core::details::Error;
using netx::core::details::error_category;
using netx::core::details::from_errno;
using netx::core::details::make_error_code;
using netx::core::details::value_or_throw;

TEST_CASE("error_category 的名称与未知错误值", "[errno]") {
    CHECK(std::string(error_category().name()) == "NetXCoreError");
    CHECK(error_category().message(9999) == "Unknown core error");
}

TEST_CASE("make_error_code 保留枚举值与类别", "[errno]") {
    auto ec = make_error_code(Error::Timeout);
    CHECK(ec.value() == static_cast<int>(Error::Timeout));
    CHECK(ec.category() == error_category());
    CHECK(ec.message() == "Operation timed out");
}

TEST_CASE("from_errno(0) 返回值为 0 的空 error_code", "[errno]") {
    auto ec = from_errno(0);
    CHECK(ec.value() == 0);
    CHECK_FALSE(static_cast<bool>(ec));
}

TEST_CASE("from_errno 把系统错误映射到核心错误码", "[errno]") {
    const std::pair<int, Error> cases[] = {
        {ETIMEDOUT, Error::Timeout},
        {ECANCELED, Error::Cancelled},
        {EPIPE, Error::BrokenPipe},
        {ECONNRESET, Error::ConnectionReset},
        {EMFILE, Error::ResourceExhausted},
        {ENFILE, Error::ResourceExhausted},
        {ENOSPC, Error::ResourceExhausted},
        {ENOBUFS, Error::ResourceExhausted},
        {ENOMEM, Error::ResourceExhausted},
        {EEXIST, Error::AlreadyStarted},
        {ENOENT, Error::InvalidOperation},
        {EBADF, Error::InvalidOperation},
        {EPERM, Error::InvalidOperation},
    };

    for (const auto &[err, want] : cases) {
        INFO("errno = " << err);
        CHECK(from_errno(err) == want);
    }
}

TEST_CASE("from_errno 对未映射的 errno 保留系统错误类别", "[errno]") {
    auto ec = from_errno(EINVAL);
    CHECK(ec.value() == EINVAL);
    CHECK(ec.category() == std::system_category());
    CHECK(ec.message() == std::generic_category().message(EINVAL));
}

TEST_CASE("Error 经由 is_error_code_enum 自动转换", "[errno]") {
    Expected<int> r = std::unexpected(Error::BrokenPipe);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == Error::BrokenPipe);
    CHECK(r.error().message() == "Broken pipe / Remote closed");
}

TEST_CASE("value_or_throw 成功时返回值", "[errno]") {
    Expected<int> r = 42;
    CHECK(value_or_throw(r) == 42);

    Expected<std::string> s = std::string(200, 'x');
    CHECK(value_or_throw(s).size() == 200);
}

TEST_CASE("value_or_throw 失败时抛 std::system_error", "[errno]") {
    Expected<int> r = std::unexpected(make_error_code(Error::Timeout));

    CHECK_THROWS_AS(value_or_throw(r), std::system_error);

    try {
        (void)value_or_throw(r);
        FAIL("value_or_throw 在失败时应当抛出异常");
    } catch (const std::system_error &e) {
        CHECK(e.code() == Error::Timeout);
        CHECK(std::string(e.what()) == "Operation timed out");
    }
}

TEST_CASE("Expected<void> 支持 value_or_throw", "[errno]") {
    Expected<void> ok{};
    CHECK_NOTHROW(value_or_throw(ok));

    Expected<void> bad = std::unexpected(make_error_code(Error::Cancelled));
    CHECK_THROWS_AS(value_or_throw(bad), std::system_error);
}

TEST_CASE("Expected<> 默认就是不携带值的 void 形式", "[errno]") {
    static_assert(
        std::is_same_v<Expected<>, std::expected<void, std::error_code>>);

    Expected<> ok{};
    CHECK_NOTHROW(value_or_throw(ok));

    // 只关心成功/失败时的典型用法：直接 return，不带值
    Expected<> bad = std::unexpected(make_error_code(Error::ConnectionReset));
    CHECK_THROWS_AS(value_or_throw(bad), std::system_error);
}

TEST_CASE("直接调用 .value() 时抛标准异常，无法定制", "[errno]") {
    // std::bad_expected_access<E> 是标准库类型，不能特化成 system_error，
    // 因此对外取值请用 value_or_throw()。
    Expected<int> r = std::unexpected(make_error_code(Error::Timeout));

    CHECK_THROWS_AS(r.value(), std::bad_expected_access<std::error_code>);

    try {
        (void)r.value();
    } catch (const std::bad_expected_access<std::error_code> &e) {
        CHECK(e.error() == Error::Timeout);
    }
}

TEST_CASE("每个 Error 枚举值都有稳定且非空的消息", "[errno]") {
    const std::pair<Error, const char *> cases[] = {
        {Error::Success, "Success"},
        {Error::Timeout, "Operation timed out"},
        {Error::Cancelled, "Operation was cancelled"},
        {Error::BrokenPipe, "Broken pipe / Remote closed"},
        {Error::ConnectionReset, "Connection reset by peer"},
        {Error::ConnectionAborted, "Connection aborted"},
        {Error::AlreadyStarted, "Task or operation already started (EEXIST)"},
        {Error::InvalidOperation,
         "Invalid operation for current state (ENOENT/EBADF/EPERM)"},
        {Error::ResourceExhausted,
         "System resource (FD/Memory/Watch) exhausted"},
    };

    // 枚举加了新值就把它补进上表 —— 这条断言是提醒，不是装饰
    CHECK(std::size(cases) == 9);

    for (const auto &[value, want] : cases) {
        INFO("Error = " << static_cast<int>(value));
        const auto ec = make_error_code(value);
        CHECK(ec.value() == static_cast<int>(value));
        CHECK(ec.category() == error_category());
        CHECK(ec.message() == want);
    }
}

TEST_CASE("make_error_to_unexpected 包出本库类别的失败 Expected", "[errno]") {
    const auto e = make_error_to_unexpected(Error::BrokenPipe);

    // 值就是那个错误的 unexpected（不是"成功的 Expected 里装个错误码"）
    static_assert(
        std::is_same_v<decltype(e), const std::unexpected<std::error_code>>);
    CHECK(e.error() == make_error_code(Error::BrokenPipe));
    CHECK(e.error().value() == static_cast<int>(Error::BrokenPipe));
    CHECK(e.error().category() == error_category());

    // 喂给 Expected 之后 operator bool 为假 —— 这才叫失败
    const Expected<> exp = e;
    CHECK_FALSE(exp.has_value());
    CHECK(exp.error() == make_error_code(Error::BrokenPipe));
}

TEST_CASE("Error::Success 的 error_code 是假值", "[errno]") {
    // 它在本库自己的类别里值也是 0，而 operator bool 看的就是 value() != 0
    CHECK_FALSE(static_cast<bool>(make_error_code(Error::Success)));
    CHECK(static_cast<bool>(make_error_code(Error::Timeout)));
}
