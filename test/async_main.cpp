#include "netx/core/async_main.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

using netx::core::async_main;
using netx::core::co_spawn;
using netx::core::Expected;
using netx::core::Task;
using netx::core::details::Error;
using netx::core::details::Event;
using netx::core::details::EventLoop;
using netx::core::details::make_error_code;
using netx::core::details::WrappedTask;

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

/// 移动-only 的结果类型：证明值是从帧里**搬**出来的，不是拷贝
Task<std::unique_ptr<int>> make_owner() {
    co_return std::make_unique<int>(7);
}

Task<std::string> hello() {
    co_return std::string{"hello"};
}

Task<Expected<int>> expected_ok() {
    Expected<int> value{7};
    co_return co_await value;
}

Task<Expected<int>> expected_fail() {
    Expected<int> value = std::unexpected(make_error_code(Error::Timeout));
    co_return co_await value;
}

Task<int> boom() {
    throw std::runtime_error{"boom"};
    co_return 0;
}

/// fd 的 RAII 包装，断言失败时也不会泄漏
class FdGuard {
  public:
    explicit FdGuard(int fd) noexcept : fd_(fd) {
    }
    FdGuard(const FdGuard &) = delete;
    FdGuard &operator=(const FdGuard &) = delete;
    ~FdGuard() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

  private:
    int fd_;
};

struct Observed {
    bool resumed{false};
};

/// 挂起在"fd 可读"上，恢复后留下痕迹
Task<> wait_readable(int fd, Observed &out) {
    co_await EventLoop::loop().wait_event(
        Event{.fd = fd, .flags = Event::kEventRead});
    out.resumed = true;
    co_return;
}

Task<> write_byte(int fd) {
    const char byte = 'x';
    const auto written = ::write(fd, &byte, 1);
    (void)written;
    co_return;
}

/// 记下自己什么时候跑，用来看 co_spawn 的入队顺序与"到底跑没跑"
Task<> record(std::vector<int> &log, int id) {
    log.push_back(id);
    co_return;
}

} // namespace

TEST_CASE("async_main 跑完任务并把结果带出来", "[async_main]") {
    CHECK(async_main(answer()) == 42);
    CHECK(async_main(twice()) == 84); // 中间 co_await 了另一个 Task
    CHECK(async_main(hello()) == "hello");
}

TEST_CASE("Task<void> 与移动-only 结果都能带出来", "[async_main]") {
    CHECK_NOTHROW(async_main(nothing()));

    auto owner = async_main(make_owner());
    REQUIRE(owner != nullptr);
    CHECK(*owner == 7);
}

TEST_CASE("T 为 Expected 时结果原样带出，不走异常", "[async_main]") {
    const auto ok = async_main(expected_ok());
    REQUIRE(ok.has_value());
    CHECK(*ok == 7);

    const auto bad = async_main(expected_fail());
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error() == Error::Timeout);
}

TEST_CASE("任务里抛出的异常由 result() 重新抛出", "[async_main]") {
    CHECK_THROWS_AS(async_main(boom()), std::runtime_error);
}

TEST_CASE("WrappedTask::result 右值按值返回、左值返回引用", "[async_main]") {
    // 右值重载曾经把 std::move 用在 awaiter 上，于是走 lvalue 分支返回帧里的
    // T&，而调用方拿到引用时帧已被 ~WrappedTask 释放。这里把接口钉死。
    using WT = WrappedTask<Task<int>>;
    static_assert(
        std::is_same_v<decltype(std::declval<WT &&>().result()), int>);
    static_assert(
        std::is_same_v<decltype(std::declval<WT &>().result()), int &>);
    SUCCEED();
}

TEST_CASE("无参 async_main 会把挂起在事件上的任务跑完", "[async_main]") {
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const FdGuard read_end{fds[0]};
    const FdGuard write_end{fds[1]};

    Observed out;
    auto reader = wait_readable(fds[0], out);
    auto writer = write_byte(fds[1]);

    // 先入队者先跑：reader 先注册 epoll 并挂起，同一轮里 writer 再写数据，
    // 于是数据是在 reader 挂起之后才到达的 —— 能跑完就说明真的被事件唤醒过。
    reader.coro.promise().schedule();
    writer.coro.promise().schedule();

    async_main();

    CHECK(out.resumed);
    CHECK(reader.done());
    CHECK(writer.done());
}

TEST_CASE("空循环上调用 async_main 直接返回", "[async_main]") {
    CHECK_NOTHROW(async_main());
}

TEST_CASE("WrappedTask 的 settled 与 done 在冻结任务上分歧", "[async_main]") {
    auto wrapped = WrappedTask{expected_fail()};
    CHECK_FALSE(wrapped.settled());

    async_main(); // 构造时会 schedule，这里把它跑掉

    CHECK_FALSE(wrapped.done()); // 冻在失败的 co_await 处
    CHECK(wrapped.settled());    // 但结果已经可取

    const auto result = std::move(wrapped).result();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == Error::Timeout);
}

TEST_CASE("co_spawn 出来的任务由无参 async_main 统一跑完", "[co_spawn]") {
    auto task = co_spawn(answer());
    CHECK_FALSE(task.settled()); // 构造时就 schedule 了，但还没跑

    async_main();

    CHECK(task.done());
    CHECK(task.settled());
    CHECK(task.result() == 42);
}

TEST_CASE("一次 async_main 跑完多个 co_spawn 的任务，按入队顺序",
          "[co_spawn]") {
    std::vector<int> log;
    auto a = co_spawn(record(log, 1));
    auto b = co_spawn(record(log, 2));
    auto c = co_spawn(record(log, 3));

    async_main();

    CHECK(a.done());
    CHECK(b.done());
    CHECK(c.done());
    CHECK(log == std::vector<int>{1, 2, 3});
}

TEST_CASE("co_spawn 的任务内部还能 co_await 别的任务", "[co_spawn]") {
    auto task = co_spawn(twice()); // twice 里 co_await 了 answer
    async_main();

    CHECK(task.done());
    CHECK(task.result() == 84);
}

TEST_CASE("co_spawn 的任务挂在事件上，由事件唤醒", "[co_spawn]") {
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const FdGuard read_end{fds[0]};
    const FdGuard write_end{fds[1]};

    Observed out;
    // 入队顺序决定谁先跑：reader 先注册 epoll 挂起，writer 再写数据，
    // 于是数据是在 reader 挂起之后才到达的
    auto reader = co_spawn(wait_readable(fds[0], out));
    auto writer = co_spawn(write_byte(fds[1]));

    async_main();

    CHECK(out.resumed);
    CHECK(reader.done());
    CHECK(writer.done());
}

TEST_CASE("co_spawn 的任务失败冻结后结果仍可取", "[co_spawn]") {
    auto task = co_spawn(expected_fail());
    async_main();

    CHECK_FALSE(task.done()); // 冻在失败的 co_await 处
    CHECK(task.settled());
    CHECK(task.result().error() == Error::Timeout);
}

TEST_CASE("丢弃 co_spawn 的返回值：任务不会被执行", "[co_spawn]") {
    std::vector<int> log;
    // [[nodiscard]] 的临时量在这个分号处就析构了，帧随之销毁 —— 这正是
    // 那条 nodiscard 提示想表达的：丢掉包装就等于丢掉这个任务
    (void)co_spawn(record(log, 1));

    async_main();

    CHECK(log.empty());
}

TEST_CASE("WrappedTask::cancel 丢掉一个还没跑的任务", "[co_spawn]") {
    std::vector<int> log;
    auto task = co_spawn(record(log, 1));
    CHECK(task.valid());

    task.cancel(); // 硬取消：立刻销毁帧

    CHECK_FALSE(task.valid());

    // 帧已经没了，取结果不能再解引用空 promise（曾经是 SIGSEGV）。返回引用
    // 无处可指，所以借异常通道交代：这是一次取消。
    CHECK_THROWS_AS(task.result(), std::system_error);

    async_main(); // 循环里已经没有它的记录了，不能碰到已释放的帧
    CHECK(log.empty());
}
