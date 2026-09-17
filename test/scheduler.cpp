// netx/net/scheduler.hpp 的测试。
//
// Scheduler 的调度循环是常驻协程，测试里不便整体驱动，这里聚焦两件能独立
// 验证、且正是修复点的事：
//   1. create() 开的 eventfd 必须由析构关掉 —— 原来没有析构函数，每建一个
//      调度器就漏一个 fd
//   2. wakeup() 能正常把 eventfd 计数加上去

#include "netx/net/scheduler.hpp"

#include "netx/core/async_main.hpp"
#include "netx/core/event_loop.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <latch>
#include <vector>

#include <cstddef>

using netx::core::async_main;
using netx::core::details::EventLoop;
using netx::core::details::Handle;
using netx::net::details::Scheduler;

namespace {

// /proc/self/fd 的条目数。遍历本身会占一个 fd，但每次都占，差值仍然有效
std::size_t open_fd_count() {
    std::size_t n = 0;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator("/proc/self/fd", ec);
         !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        ++n;
    }
    return n;
}

/// 被调度的任务：记一笔，顺手把常驻的调度循环取消掉。
/// 调度循环永不正常返回，不给自己一个收工的理由，事件循环就永远非空、
/// async_main() 也就永远不返回。
netx::core::Task<netx::core::Expected<>> record_and_stop(EventLoop &loop,
                                                         Handle &target,
                                                         std::vector<int> &log,
                                                         int id) {
    log.push_back(id);
    loop.cancel(target);
    co_return {};
}

} // namespace

TEST_CASE("scheduler_loop 会执行推入队列的任务", "[net][scheduler]") {
    auto sched = Scheduler::create();
    REQUIRE(sched.has_value());
    if (!sched) {
        return;
    }

    auto &loop = EventLoop::loop();
    std::latch started{1};
    std::vector<int> log;

    // 把常驻的调度循环排进事件循环
    auto loop_task = sched->scheduler_loop(started);
    auto &loop_handle = loop_task.coro.promise();
    loop.call_soon(loop_handle);

    // 推一个任务并唤醒：唤醒 -> drain 队列 -> 登记 -> 调度 -> 执行，中途要
    // 经过好几轮事件循环；最后一轮里任务取消常驻循环，async_main 才收得了工
    sched->push(record_and_stop(loop, loop_handle, log, 7));
    REQUIRE(sched->wakeup().has_value());

    async_main();

    CHECK(started.try_wait()); // 循环体确实开始执行了
    CHECK(log == std::vector<int>{7});
}

TEST_CASE("Scheduler 析构会关掉 create 开的 eventfd", "[net][scheduler]") {
    // 先把 thread_local 事件循环建出来：它自己会开一个 epoll fd，
    // 不先建好就会把那次增长算到 Scheduler 头上
    (void)EventLoop::loop();

    const std::size_t before = open_fd_count();

    {
        auto sched = Scheduler::create();
        REQUIRE(sched.has_value());

        // 确实开出了 eventfd
        CHECK(open_fd_count() > before);

        // 唤醒走 eventfd 写入，应当成功
        CHECK(sched->wakeup().has_value());
    }

    // 析构之后 fd 要还回去 —— 少了析构函数这里会多一个
    CHECK(open_fd_count() == before);
}

TEST_CASE("wakeup 可以重复调用", "[net][scheduler]") {
    auto sched = Scheduler::create();
    REQUIRE(sched.has_value());

    for (int i = 0; i < 100; ++i) {
        CHECK(sched->wakeup().has_value());
    }
}

TEST_CASE("新调度器大小为 0，队列里的任务不计入 size", "[net][scheduler]") {
    auto sched = Scheduler::create();
    REQUIRE(sched.has_value());

    CHECK(sched->size() == 0);

    // size() 数的是已经登记进调度循环、正在跑的任务；push 只是丢进
    // 无锁队列，要等循环被唤醒后才登记，所以这里不该变
    sched->push(
        []() -> netx::core::Task<netx::core::Expected<>> { co_return {}; }());
    CHECK(sched->size() == 0);
}
