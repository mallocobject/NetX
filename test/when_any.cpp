#include "netx/core/when_any.hpp"
#include "netx/core/async_main.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/sleep.hpp"
#include "netx/core/task.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;

using netx::core::async_main;
using netx::core::Expected;
using netx::core::sleep;
using netx::core::Task;
using netx::core::when_any;
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

/// 睡够就交出 tag；被取消就把取消结果带出来 —— 顺便在外部留个痕，
/// 好观察"输家到底有没有被取消"。
Task<Expected<std::string>> tagged(std::chrono::milliseconds d,
                                   std::string tag,
                                   std::vector<std::string> &log) {
    const auto exp = co_await sleep(d);
    if (!exp) {
        log.push_back(tag + ":取消");
        co_return std::unexpected{exp.error()};
    }
    log.push_back(tag + ":完成");
    co_return std::string{tag};
}

Task<Expected<std::string>> boom_after(std::chrono::milliseconds d) {
    co_await sleep(d);
    throw std::runtime_error{"boom"};
    co_return std::string{};
}

/// 裸 Task<int>：槽位就是 int，不是 Expected。修 WhenAnyHelper 之前，
/// 这类输入会因为 int& 绑不上形参 Expected<T>& 而整条 when_any 编译不过。
Task<int> delayed_value(std::chrono::milliseconds d, int v) {
    (void)co_await sleep(d);
    co_return v;
}

bool logged(const std::vector<std::string> &log, const std::string &what) {
    return std::find(log.begin(), log.end(), what) != log.end();
}

} // namespace

TEST_CASE("when_any 挑出最先完成的那个", "[when_any]") {
    std::vector<std::string> log;

    auto r = async_main(
        when_any(tagged(50ms, "slow", log), tagged(1ms, "fast", log)));

    REQUIRE(r.has_value());
    CHECK(r->index() == 1); // 1ms 那个赢
    REQUIRE(std::get<1>(*r).has_value());
    CHECK(*std::get<1>(*r) == "fast");
    CHECK(logged(log, "fast:完成"));
}

TEST_CASE("when_any 把输家取消掉，不让它拖住循环", "[when_any]") {
    std::vector<std::string> log;

    const auto start = steady_clock::now();
    auto r = async_main(
        when_any(tagged(500ms, "slow", log), tagged(1ms, "fast", log)));
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(r.has_value());
    CHECK(r->index() == 1);

    // 两件事都说明取消真的传到了输家身上、并且把它的定时条目摘掉了：
    // 1) 输家自己观察到被取消；2) 循环没有为那 500ms 等下去
    CHECK(logged(log, "slow:取消"));
    CHECK(elapsed < 100ms);
}

TEST_CASE("void 输入的结果也能带出来", "[when_any]") {
    auto r = async_main(when_any(sleep(50ms), sleep(1ms)));

    REQUIRE(r.has_value());
    CHECK(r->index() == 1);
    CHECK(std::get<1>(*r).has_value());
}

TEST_CASE("when_any 把赢家的异常原样抛出", "[when_any]") {
    CHECK_THROWS_AS(async_main(when_any(boom_after(1ms), sleep(500ms))),
                    std::runtime_error);
}

TEST_CASE("when_any 的结果任务可以留到之后再跑", "[when_any]") {
    std::vector<std::string> log;

    // 实参是临时量：它们的生命周期必须由返回的任务接管。
    // 这条断言只在 ASan 下才有杀伤力 —— 退回按引用转发的写法会报
    // stack-use-after-scope。
    auto task = when_any(tagged(50ms, "slow", log), tagged(1ms, "fast", log));
    auto r = async_main(std::move(task));

    REQUIRE(r.has_value());
    CHECK(r->index() == 1);
    CHECK(*std::get<1>(*r) == "fast");
}

TEST_CASE("输入全都无效时给出 InvalidOperation", "[when_any]") {
    auto r = async_main(
        when_any(Task<Expected<>>{nullptr}, Task<Expected<>>{nullptr}));

    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == Error::InvalidOperation);
}

TEST_CASE("输入是裸 Task<T> 时槽位就是 T，不再要求 Expected", "[when_any]") {
    auto r = async_main(
        when_any(delayed_value(50ms, 111), delayed_value(1ms, 222)));

    REQUIRE(r.has_value());
    CHECK(r->index() == 1);       // 1ms 那个赢
    CHECK(std::get<1>(*r) == 222); // variant<int, int>，按下标取
}

TEST_CASE("裸 Task<T> 与 Task<Expected<T>> 能混在同一次竞速里", "[when_any]") {
    auto r = async_main(when_any(delayed_value(1ms, 7), sleep(50ms)));

    REQUIRE(r.has_value());
    CHECK(r->index() == 0);
    CHECK(std::get<0>(*r) == 7);
}

TEST_CASE("裸 Task<T> 的输家同样被取消，不拖住循环", "[when_any]") {
    const auto start = steady_clock::now();
    auto r = async_main(
        when_any(delayed_value(500ms, 1), delayed_value(1ms, 2)));
    const auto elapsed = steady_clock::now() - start;

    REQUIRE(r.has_value());
    CHECK(r->index() == 1);
    CHECK(elapsed < 100ms); // 500ms 那条被摘掉了
}

TEST_CASE("取消 when_any 本身：晚到的赢家不会把它复活", "[when_any]") {
    auto &loop = EventLoop::loop();
    std::vector<std::string> log;

    auto task = when_any(tagged(50ms, "a", log), tagged(200ms, "b", log));
    Killer killer{task.coro.promise()};

    task.coro.promise().schedule();
    loop.call_after(5ms, killer); // 它挂起等待之后再取消
    async_main();

    // 取消一个“没有挂起记录”的协程就是给它一个 Cancelled 结果；
    // 若赢家之后又把它唤醒，它会从挂起点继续跑并把结果覆盖成成功 ——
    // 那才是真正的复活。
    CHECK(task.settled());
    REQUIRE_FALSE(task.coro.promise().result().has_value());
    CHECK(task.coro.promise().result().error() == Error::Cancelled);

    // 已知缺口：helper 不会跟着取消，所以赢家还是会跑完（结果被丢掉）。
    // 它们的帧归 when_any 的帧所有，不泄漏。
    CHECK(logged(log, "a:完成"));
}
