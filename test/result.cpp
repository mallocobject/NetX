#include "netx/core/result.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using netx::core::details::Result;

TEST_CASE("return_value 写入后可由左值 result() 取出", "[result]") {
    Result<int> r;
    r.return_value(42);
    CHECK(r.result() == 42);
}

TEST_CASE("put_value 在空状态下构造、在已有值时走赋值分支", "[result]") {
    Result<int> r;
    r.put_value(1);
    CHECK(r.result() == 1);

    r.put_value(2);
    CHECK(r.result() == 2);
}

TEST_CASE("右值 result() 移出存储的值", "[result]") {
    Result<int> r;
    r.return_value(7);

    int moved = std::move(r).result();
    CHECK(moved == 7);
}

TEST_CASE("Result<void> 返回 NonVoidHelper", "[result]") {
    Result<void> r;
    r.return_void();
    CHECK_NOTHROW(r.result());
}

TEST_CASE("Result<const T> 与 Result<T&&> 复用主模板语义", "[result]") {
    Result<const int> r;
    r.return_value(3);
    CHECK(r.result() == 3);

    Result<int &&> rv;
    rv.put_value(4);
    CHECK(rv.result() == 4);
}

// 统计存活对象数，用于验证结果值只被析构一次
struct Tracker {
    static inline int live = 0;

    Tracker() {
        ++live;
    }
    Tracker(const Tracker &) {
        ++live;
    }
    Tracker(Tracker &&) noexcept {
        ++live;
    }
    Tracker &operator=(const Tracker &) = default;
    Tracker &operator=(Tracker &&) = default;
    ~Tracker() {
        --live;
    }
};

TEST_CASE("非平凡类型可默认构造并正常存取", "[result]") {
    // 匿名 union 成员的默认构造非平凡时，需要显式默认构造才能实例化
    Result<std::string> s;
    s.return_value(std::string{"hello"});
    CHECK(s.result() == "hello");

    s.put_value(std::string(200, 'x')); // 堆分配，走赋值分支
    CHECK(s.result().size() == 200);

    Result<std::unique_ptr<int>> u;
    u.put_value(std::make_unique<int>(5));
    CHECK(*u.result() == 5);
}

TEST_CASE("右值 result() 移出值后不会重复析构", "[result]") {
    Tracker::live = 0;
    {
        Result<Tracker> r;
        r.return_value(Tracker{});
        CHECK(Tracker::live == 1);

        Tracker out = std::move(r).result();
        CHECK(Tracker::live == 1);
    }
    // 若 result() 移出并析构后未把状态置回 kEmpty，析构函数会再析构一次，
    // 这里就会读到 -1
    CHECK(Tracker::live == 0);
}

TEST_CASE("异常由 result() 重新抛出", "[result]") {
    // 回归守卫：unhandled_exception() 曾经只赋值 exception_、没把 state_ 置成
    // kException，result() 于是落进 state_ != kValue 的 std::terminate() 分支
    // （SIGABRT），而不是重抛。
    Result<int> r;
    try {
        throw std::runtime_error{"boom"};
    } catch (...) {
        r.unhandled_exception();
    }
    CHECK_THROWS_AS(r.result(), std::runtime_error);
}
