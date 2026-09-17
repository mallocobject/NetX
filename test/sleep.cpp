#include "netx/core/sleep.hpp"
#include "netx/core/async_main.hpp"
#include "netx/core/task.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

using netx::core::async_main;
using netx::core::co_spawn;
using netx::core::Expected;
using netx::core::sleep;
using netx::core::sleep_started;
using netx::core::Task;
using netx::core::details::Error;
using netx::core::details::EventLoop;
using netx::core::details::Handle;
using std::chrono::steady_clock;

namespace {

/// 到点就把目标取消掉（取消只能在循环内部触发）
class Killer : public Handle {
  public:
    explicit Killer(Handle &target) noexcept : target_(target) {
    }

    void run() override {
        EventLoop::loop().cancel(target_);
    }

  private:
    Handle &target_;
};

/// 睡够之后才记一笔。注意 sleep 返回 Expected<>，必须判结果 ——
/// 被取消的 sleep 不该记这一笔。
Task<>
sleep_then_log(std::chrono::milliseconds d, int id, std::vector<int> &log) {
    if (const auto exp = co_await sleep(d); exp.has_value()) {
        log.push_back(id);
    }
    co_return;
}

} // namespace

TEST_CASE("sleep 睡够时长后成功返回", "[sleep]") {
    const auto start = steady_clock::now();
    const auto result = async_main(sleep(20ms));
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(result.has_value());
    CHECK(elapsed >= 18ms); // 提前返回就说明定时器没生效
    CHECK(elapsed < 2s);
}

TEST_CASE("多个 sleep 按到期时间而不是入队顺序唤醒", "[sleep]") {
    std::vector<int> log;
    auto long_one = sleep_then_log(30ms, 1, log);
    auto short_one = sleep_then_log(1ms, 2, log);

    // 先入队的是长的那个：若按 FIFO 执行，log 就是 {1, 2}
    long_one.coro.promise().schedule();
    short_one.coro.promise().schedule();
    async_main();

    CHECK(log == std::vector<int>{2, 1});
}

TEST_CASE("取消正在 sleep 的任务：以 Cancelled 收尾且不等满时长", "[sleep]") {
    auto &loop = EventLoop::loop();

    auto task = sleep(10s);
    Killer killer{task.coro.promise()};

    task.coro.promise().schedule(); // 先起来，挂到内层 sleep 上
    loop.call_soon(killer);         // 紧随其后取消它

    const auto start = steady_clock::now();
    async_main();
    const auto elapsed = steady_clock::now() - start;

    CHECK(task.settled());
    REQUIRE_FALSE(task.coro.promise().result().has_value());
    CHECK(task.coro.promise().result().error() == Error::Cancelled);
    CHECK(elapsed < 1s); // 定时器条目必须被摘掉，否则这里会真的等 10 秒
}

TEST_CASE("取消一个 sleep 不影响另一个，也不留下定时器", "[sleep]") {
    auto &loop = EventLoop::loop();
    std::vector<int> log;

    auto fast = sleep_then_log(1ms, 1, log);
    auto slow = sleep_then_log(50ms, 2, log);
    Killer killer{slow.coro.promise()};

    fast.coro.promise().schedule();
    slow.coro.promise().schedule();
    loop.call_soon(killer); // 两个都挂到定时器之后再取消慢的那个

    const auto start = steady_clock::now();
    async_main();
    const auto elapsed = steady_clock::now() - start;

    CHECK(log == std::vector<int>{1}); // 被取消的那个不该记这一笔
    CHECK(fast.done());                // 睡够 → 记一笔 → 正常收尾
    CHECK(slow.done()); // 被唤醒后判出 Cancelled，跳过记录，也正常收尾
    CHECK(elapsed < 40ms); // 50ms 那条定时记录被摘掉了，循环不必为它等
}

TEST_CASE("co_spawn 出来的 sleep 由无参 async_main 跑完", "[sleep]") {
    auto task = co_spawn(sleep(1ms));
    CHECK_FALSE(task.settled());

    async_main();

    CHECK(task.done());
    CHECK(task.settled());
    CHECK(task.result().has_value());
}

TEST_CASE("sleep_started 返回时定时器已在循环上，sleep 还没有", "[sleep]") {
    auto &loop = EventLoop::loop();

    auto started = sleep_started(10s);
    auto lazy = sleep(10s);

    // 不用等、不用调度：循环对这两个任务的态度就是差别本身
    CHECK(loop.pending(started.coro.promise()));
    CHECK_FALSE(loop.pending(lazy.coro.promise()));

    CHECK_FALSE(started.settled()); // 都还没到点
    CHECK_FALSE(lazy.settled());
    // 作用域结束时两条 Task 析构 → detach() 会把定时条目摘掉，
    // 不会用 10 秒拖住后面的用例
}

TEST_CASE("只有 sleep_started 能自己跑完，sleep 得有人调度", "[sleep]") {
    auto started = sleep_started(1ms);
    async_main(); // 只把循环跑空，不额外调度
    CHECK(started.settled());

    auto lazy = sleep(1ms);
    async_main();
    CHECK_FALSE(lazy.settled()); // 没人调度它 → 一动不动
    CHECK(lazy.valid());         // 帧还在，等持有者处置
}

TEST_CASE("sleep_started 的计时从创建开始，sleep 从交给循环开始", "[sleep]") {
    constexpr auto kPause = 40ms;
    constexpr auto kSleep = 100ms;

    const auto eager_start = steady_clock::now();
    auto eager = sleep_started(kSleep);
    std::this_thread::sleep_for(kPause); // 定时器在创建时就已经挂好了
    async_main();
    const auto eager_elapsed = steady_clock::now() - eager_start;

    const auto lazy_start = steady_clock::now();
    auto lazy = sleep(kSleep);
    std::this_thread::sleep_for(kPause); // 这段白等，计时还没开始
    const auto lazy_result = async_main(std::move(lazy));
    const auto lazy_elapsed = steady_clock::now() - lazy_start;

    CHECK(eager.settled());
    CHECK(eager.coro.promise().result().has_value());
    REQUIRE(lazy_result.has_value());

    CHECK(eager_elapsed >= kSleep - 5ms); // 再怎么说也不会提前醒
    CHECK(lazy_elapsed >= eager_elapsed + 25ms); // 差的就是那 40ms 起算点
}

TEST_CASE("取消 sleep_started 与取消 sleep 表现一致", "[sleep]") {
    auto &loop = EventLoop::loop();

    auto task = sleep_started(10s);
    Killer killer{task.coro.promise()};
    loop.call_soon(killer); // 它已经在定时表里了，直接取消

    const auto start = steady_clock::now();
    async_main();
    const auto elapsed = steady_clock::now() - start;

    CHECK(task.settled());
    REQUIRE_FALSE(task.coro.promise().result().has_value());
    CHECK(task.coro.promise().result().error() == Error::Cancelled);
    CHECK(elapsed < 1s); // 10 秒那条定时记录被摘掉了
}

TEST_CASE("co_spawn 一个已经起跑的 sleep_started 不会重复调度", "[sleep]") {
    // sleep_started 在创建时就占了定时槽，co_spawn 的 schedule() 会被
    // 幂等守卫挡下，不会变成两份记录
    auto task = co_spawn(sleep_started(1ms));
    async_main();

    CHECK(task.done());
    CHECK(task.result().has_value());
}
