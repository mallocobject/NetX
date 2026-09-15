#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <system_error>
#include <unistd.h>

using namespace std::chrono_literals;

using netx::core::Expected;
using netx::core::Task;
using netx::core::details::Error;
using netx::core::details::Event;
using netx::core::details::EventLoop;
using netx::core::details::Handle;
using netx::core::details::make_error_code;

namespace {

/// 到点就把目标取消掉。取消必须在循环内部触发 —— run_until_complete() 会
/// 一直跑到没有待办为止，外面没机会插手。
class Canceller : public Handle {
  public:
    explicit Canceller(Handle &target) noexcept : target_(target) {
    }

    void run() override {
        EventLoop::loop().cancel(target_);
    }

  private:
    Handle &target_;
};

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

struct Flags {
    bool child_ran{false};
    bool grandchild_ran{false};
    bool first_was_cancelled{false};
};

Task<Expected<int>> grandchild(Flags &flags) {
    flags.grandchild_ran = true;
    co_return Expected<int>{7};
}

Task<Expected<int>> child(Flags &flags) {
    flags.child_ran = true;
    co_return co_await grandchild(flags);
}

/// 三层链：parent -> child -> grandchild
Task<Expected<int>> parent(Flags &flags) {
    co_return co_await child(flags);
}

/// 一直挂在 fd 可读上，谁都不写就对端
Task<Expected<>> waits_forever(int fd) {
    co_return co_await EventLoop::loop().wait_event(
        Event{.fd = fd, .flags = Event::kEventRead});
}

Task<Expected<>> parent_of_waiter(int fd) {
    co_return co_await waits_forever(fd);
}

Task<Expected<>> child_of_late(Flags &flags) {
    flags.child_ran = true;
    co_return Expected<>{};
}

/// 先被取消唤醒（协作式），再去 co_await 一个子任务 —— 那个子任务属于
/// "取消之后才轮到上场"的迟到者
Task<Expected<>> late_parent(int fd, Flags &flags) {
    const auto first = co_await EventLoop::loop().wait_event(
        Event{.fd = fd, .flags = Event::kEventRead});
    flags.first_was_cancelled = !first.has_value();
    co_return co_await child_of_late(flags);
}

Task<int> answer() {
    co_return 42;
}

/// 什么都没做就结束的 Task<>：用来试"取消一个还没跑起来的 void 任务"
Task<> never_started() {
    co_return;
}

Task<Expected<int>> expects_timeout() {
    Expected<int> value = std::unexpected(make_error_code(Error::Timeout));
    co_return co_await value;
}

} // namespace

TEST_CASE("取消父协程会沿 co_await 链向下取消整条链", "[cancel]") {
    auto &loop = EventLoop::loop();
    Flags flags;

    auto task = parent(flags);
    Canceller killer{task.coro.promise()};

    // 先让父跑起来挂到子任务上，紧接着取消它
    task.coro.promise().schedule();
    loop.call_soon(killer);
    loop.run_until_complete();

    // 链上还没轮到的任务一行都没执行
    CHECK_FALSE(flags.child_ran);
    CHECK_FALSE(flags.grandchild_ran);

    // 但父协程被唤醒、读到了取消结果，并正常收尾
    CHECK(task.done());
    REQUIRE(task.coro.promise().result().has_value() == false);
    CHECK(task.coro.promise().result().error() == Error::Cancelled);
}

TEST_CASE("子任务挂起在事件上时，取消父协程它也能收尾", "[cancel]") {
    auto &loop = EventLoop::loop();
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const FdGuard read_end{fds[0]};
    const FdGuard write_end{fds[1]};

    auto task = parent_of_waiter(fds[0]);
    Canceller killer{task.coro.promise()};

    task.coro.promise().schedule();
    // 等子任务把 fd 注册到 epoll 上之后再取消；用定时器把这一枪排到下一轮
    loop.call_after(1ms, killer);
    loop.run_until_complete();

    CHECK(task.done());
    const auto result = task.coro.promise().result();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == Error::Cancelled);
}

TEST_CASE("被取消的父协程不会再启动新的子任务", "[cancel]") {
    auto &loop = EventLoop::loop();
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const FdGuard read_end{fds[0]};
    const FdGuard write_end{fds[1]};

    Flags flags;
    auto task = late_parent(fds[0], flags);
    Canceller killer{task.coro.promise()};

    task.coro.promise().schedule();
    loop.call_soon(killer);
    loop.run_until_complete();

    CHECK(flags.first_was_cancelled); // 第一次挂起是被取消唤醒的
    CHECK_FALSE(flags.child_ran); // 迟到的那次 co_await 没把子任务跑起来
    CHECK(task.done());
    CHECK(task.coro.promise().result().error() == Error::Cancelled);
}

TEST_CASE("结果类型装不下 error_code 时，取消结果借异常通道送出", "[cancel]") {
    auto &loop = EventLoop::loop();

    auto task = answer(); // Task<int>
    Canceller killer{task.coro.promise()};

    loop.call_soon(killer); // 先取消，任务还没轮到跑
    task.coro.promise().schedule();
    loop.run_until_complete();

    CHECK(task.settled());
    CHECK_FALSE(task.done()); // 没跑过，停在 initial_suspend
    CHECK_THROWS_AS(std::move(task.coro.promise()).result(), std::system_error);
}

TEST_CASE("Task<void> 被取消：result() 抛 system_error 且 settled 为真",
          "[cancel]") {
    auto &loop = EventLoop::loop();

    auto task = never_started(); // Task<>
    Canceller killer{task.coro.promise()};

    loop.call_soon(killer); // 先取消，任务还没轮到跑
    task.coro.promise().schedule();
    loop.run_until_complete();

    // 定局了：现在调 result() 会抛 Cancelled，而不是 terminate
    CHECK(task.settled());
    CHECK_FALSE(task.done()); // 停在 initial_suspend，没跑过
    CHECK_THROWS_AS(std::move(task.coro.promise()).result(), std::system_error);
}

TEST_CASE("重复取消不会覆盖已经定局的结果", "[cancel]") {
    auto &loop = EventLoop::loop();

    auto task = expects_timeout();
    task.coro.promise().schedule();
    loop.run_until_complete();

    REQUIRE(task.coro.promise().result().error() == Error::Timeout);

    task.coro.promise().request_cancel(); // 冻结的结果不该被改成 Cancelled
    CHECK(task.coro.promise().result().error() == Error::Timeout);
}
