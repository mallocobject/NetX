#include "netx/core/task.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <system_error>

using netx::core::Expected;
using netx::core::Task;
using netx::core::details::Error;
using netx::core::details::EventLoop;
using netx::core::details::Future;
using netx::core::details::make_error_code;
using netx::core::details::Promise;

namespace {

Task<int> answer() {
    co_return 42;
}

Task<int> twice() {
    const int value = co_await answer();
    co_return value * 2;
}

Task<> nothing() {
    co_return;
}

Task<> boom_void() {
    throw std::runtime_error{"boom"};
    co_return;
}

Task<Expected<int>> expected_ok() {
    Expected<int> value{7};
    co_return co_await value;
}

Task<Expected<int>> expected_fail() {
    Expected<int> value = std::unexpected(make_error_code(Error::Timeout));
    co_return co_await value;
}

/// 空协程帧的任务（没有 promise 可读）
Task<Expected<>> empty_task() {
    return Task<Expected<>>{nullptr};
}

/// co_await 一个空壳任务。after 用来观察挂起点之后那行到底有没有被执行。
Task<Expected<>> awaits_empty(bool &after) {
    co_await empty_task();
    after = true;
    co_return std::unexpected{make_error_code(Error::Timeout)};
}

/// 上层任务：读下层冻结时写下的结果，再原样带出去
Task<Expected<>> nests_empty(bool &resumed) {
    const auto exp = co_await awaits_empty(resumed);
    resumed = true;
    co_return exp;
}

Task<int> awaits_empty_int() {
    Task<int> empty{nullptr};
    co_return co_await std::move(empty);
}

/// co_await 一个左值 Task：既不是临时量，也没有 std::move，走的是
/// await_transform(Task<U>&) 那条重载。
Task<int> awaits_lvalue(Task<int> &child) {
    const int value = co_await child;
    co_return value + 1;
}

} // namespace

TEST_CASE("Task 的 promise_type 满足 Promise / Future 概念", "[task]") {
    // 这两条断言要求 promise_type、Task 的构造/析构/移动都可访问，
    // 且 promise_type 公开继承 CoroHandle 与 Result<T>。
    static_assert(Promise<Task<>::promise_type>);
    static_assert(Future<Task<>>);
}

TEST_CASE("Task 是惰性的，schedule 之后由事件循环执行", "[task]") {
    auto task = answer();

    CHECK(task.valid());
    CHECK_FALSE(task.done()); // initial_suspend 挂起，还没开始跑

    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    CHECK(task.done());
    CHECK(task.coro.promise().result() == 42);
}

TEST_CASE("Task 之间可以 co_await 串联", "[task]") {
    auto task = twice();

    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    CHECK(task.done());
    CHECK(task.coro.promise().result() == 84);
}

TEST_CASE("co_await 左值 Task：走 await_transform(Task<U>&)，帧不转移", "[task]") {
    auto child = answer(); // 左值：所有权还在本用例手上
    auto parent = awaits_lvalue(child);

    parent.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    CHECK(parent.done());
    CHECK(parent.coro.promise().result() == 43);

    // 被 co_await 的子任务并没有把帧交给父协程 —— 它自己跑完，结果仍可取
    CHECK(child.done());
    CHECK(child.coro.promise().result() == 42);
}

TEST_CASE("Task<void> 可以正常跑完", "[task]") {
    auto task = nothing();

    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    CHECK(task.done());
}

TEST_CASE("T 声明为 Expected 时成功路径直接取值", "[task]") {
    auto task = expected_ok();

    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    CHECK(task.done());
    const auto result = task.coro.promise().result();
    REQUIRE(result.has_value());
    CHECK(*result == 7);
}

TEST_CASE("T 声明为 Expected 时失败以 error_code 存进结果", "[task]") {
    auto task = expected_fail();

    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    // 任务在 co_await 处被放弃（await_suspend 挂起自己、只唤醒 continuation），
    // 失败信息随任务结果原样带出，不需要异常参与。
    CHECK_FALSE(task.done());

    const auto result = task.coro.promise().result();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == Error::Timeout);
}

TEST_CASE("settled 覆盖未跑 / 跑完 / 冻结三种状态", "[task]") {
    auto pending = answer();
    CHECK_FALSE(pending.done());
    CHECK_FALSE(pending.settled());

    pending.coro.promise().schedule();
    EventLoop::loop().run_until_complete();
    CHECK(pending.done());
    CHECK(pending.settled());

    // 冻结：失败已写进结果，但协程没跑到 final_suspend
    auto frozen = expected_fail();
    CHECK_FALSE(frozen.settled());
    frozen.coro.promise().schedule();
    EventLoop::loop().run_until_complete();
    CHECK_FALSE(frozen.done());
    CHECK(frozen.settled());

    // Task<void>：跑完也算定局
    auto nil = nothing();
    CHECK_FALSE(nil.settled());
    nil.coro.promise().schedule();
    EventLoop::loop().run_until_complete();
    CHECK(nil.done());
    CHECK(nil.settled());
}

TEST_CASE("Task<void> 抛出的异常由 result() 重抛", "[task]") {
    // 回归守卫：Result<void>::result() 曾经标了 noexcept，那次 rethrow 会
    // 直接 terminate，调用方的 catch 根本接不到。
    auto task = boom_void();
    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    CHECK(task.done());
    CHECK(task.settled());
    CHECK_THROWS_AS(std::move(task.coro.promise()).result(),
                    std::runtime_error);
}

TEST_CASE("用右值 result() 取走值之后结果不再可取", "[task]") {
    auto task = answer();
    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    REQUIRE(std::move(task.coro.promise()).result() == 42);

    CHECK(task.done());          // 协程确实跑到了 final_suspend
    CHECK_FALSE(task.settled()); // 值已被搬走，再取会 terminate
}

TEST_CASE("co_await 空壳 Task：调用者按取消收场，不去解引用空 promise",
          "[task]") {
    bool after = false;
    auto task = awaits_empty(after);

    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    // 曾经的行为：await_ready() 对空壳返回 true（假装"已完成"），紧接着
    // await_resume() 去解引用空 promise —— SIGSEGV。现在的行为是等它等于
    // 被取消：结果已定、协程冻在挂起点。
    CHECK(task.settled());
    CHECK_FALSE(task.done());
    CHECK_FALSE(after);

    const auto result = task.coro.promise().result();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == Error::Cancelled);
}

TEST_CASE("空壳任务的取消会沿等待链传上去，等待者不会被落下", "[task]") {
    bool resumed = false;
    auto task = nests_empty(resumed);

    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    // 下层冻在挂起点，但它把上层叫醒了（notify_continuation 那一步）——
    // 否则上层就永远停在 co_await 上，而 co_await 一个 Task<Expected<>>
    // 的结果本来就是一个 Expected，取消信息是这样一层层交上去的。
    CHECK(resumed);
    CHECK(task.done());

    const auto result = task.coro.promise().result();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == Error::Cancelled);
}

TEST_CASE("空壳任务的结果装不下 error_code 时借异常通道送出", "[task]") {
    auto task = awaits_empty_int();

    task.coro.promise().schedule();
    EventLoop::loop().run_until_complete();

    CHECK(task.settled());
    CHECK_FALSE(task.done());
    CHECK_THROWS_AS(task.coro.promise().result(), std::system_error);
}
