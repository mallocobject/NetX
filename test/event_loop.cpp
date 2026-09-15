#include "netx/core/event_loop.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <coroutine>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

using netx::core::details::Epoller;
using netx::core::details::Error;
using netx::core::details::Event;
using netx::core::details::EventLoop;
using netx::core::details::Handle;
using netx::core::details::HandleId;

namespace {

/// 只记录"我被执行过"，用来断言调度顺序
class Recorder : public Handle {
  public:
    explicit Recorder(std::vector<HandleId> &log) noexcept : log_(log) {
    }

    void run() override {
        log_.push_back(id);
    }

  private:
    std::vector<HandleId> &log_;
};

/// fd 的 RAII 包装，避免断言失败时泄漏
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

/// 最小可运行的协程：promise 同时继承 Handle，这样 EventLoop 能直接把它
/// 当成调度单位调用 run()，而 run() 负责恢复协程。
class Coro {
  public:
    struct promise_type : Handle {
        std::coroutine_handle<promise_type> coroutine{};

        Coro get_return_object() noexcept {
            // 必须把它存进 promise 自己：EventLoop 是通过 run() 恢复协程的
            coroutine =
                std::coroutine_handle<promise_type>::from_promise(*this);
            return Coro{coroutine};
        }

        std::suspend_never initial_suspend() const noexcept {
            return {};
        }

        std::suspend_always final_suspend() noexcept {
            return {};
        }

        void return_void() const noexcept {
        }

        void unhandled_exception() const noexcept {
            std::terminate();
        }

        void run() override {
            coroutine.resume();
        }
    };

    Coro() = delete;
    explicit Coro(std::coroutine_handle<promise_type> handle) noexcept
        : handle_(handle) {
    }
    Coro(const Coro &) = delete;
    Coro &operator=(const Coro &) = delete;
    Coro(Coro &&other) noexcept : handle_(std::exchange(other.handle_, {})) {
    }
    Coro &operator=(Coro &&other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~Coro() {
        if (handle_) {
            handle_.destroy();
        }
    }

    [[nodiscard]] bool finished() const noexcept {
        return handle_.done();
    }

    /// 拿到帧里的 promise，方便测试直接操作它的 Handle 状态
    [[nodiscard]] promise_type &promise() const noexcept {
        return handle_.promise();
    }

  private:
    std::coroutine_handle<promise_type> handle_;
};

struct Observed {
    bool resumed{false};
    bool ok{false};
    std::error_code error{};
};

/// co_await 到可读事件为止；恢复后把结果写回 out
Coro await_readable(EventLoop &loop, int fd, Observed &out) {
    const auto exp =
        co_await loop.wait_event(Event{.fd = fd, .flags = Event::kEventRead});
    out.ok = exp.has_value();
    if (!exp) {
        out.error = exp.error();
    }
    out.resumed = true;
}

} // namespace

TEST_CASE("call_soon 按 FIFO 顺序执行并清空挂起记录", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;
    Recorder a{log};
    Recorder b{log};
    Recorder c{log};

    loop.call_soon(a);
    loop.call_soon(b);
    loop.call_soon(c);

    CHECK(loop.pending(a));
    CHECK(loop.pending(b));

    loop.run_until_complete();

    REQUIRE(log.size() == 3);
    CHECK(log[0] == a.id);
    CHECK(log[1] == b.id);
    CHECK(log[2] == c.id);
    CHECK_FALSE(loop.pending(a));
    CHECK_FALSE(loop.pending(c));
}

TEST_CASE("call_soon 忽略已取消的 handle", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;
    Recorder handle{log};

    handle.cancelled = true;
    loop.call_soon(handle);

    // 没有任何待办事项，循环应当立刻判定为已停止
    loop.run_until_complete();

    CHECK(log.empty());
    CHECK_FALSE(loop.pending(handle));
}

TEST_CASE("call_after 按到期时间而非插入顺序执行", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;
    Recorder slow{log};
    Recorder fast{log};

    // 起点必须在注册**之前**取：60ms 的到期时刻是从注册那一刻起算的，
    // 注册之后再取起点会把注册耗掉的时间算进去，慢环境（valgrind / TSan）
    // 下就会量出不到 60ms 的间隔而误报。
    const auto begin = std::chrono::steady_clock::now();

    // 故意先注册晚到期的
    loop.call_after(60ms, slow);
    loop.call_after(10ms, fast);
    CHECK(loop.pending(slow));
    CHECK(loop.pending(fast));

    loop.run_until_complete();
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    REQUIRE(log.size() == 2);
    CHECK(log[0] == fast.id);
    CHECK(log[1] == slow.id);
    CHECK(elapsed >= 58ms); // 从现在起至少等到 60ms 那条
}

TEST_CASE("call_at 收绝对时刻：过去的立刻到，未来的等到点", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;
    Recorder past{log};
    Recorder future{log};

    const auto base = std::chrono::steady_clock::now();

    loop.call_at(base - 1ms, past); // 已经过期
    REQUIRE(loop.pending(past));

    const auto begin = std::chrono::steady_clock::now();
    loop.run_until_complete();
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    // 过期的时刻会被 clamp 到 0，不该让循环等到 poll 超时
    REQUIRE(log == std::vector<HandleId>{past.id});
    CHECK(elapsed < 200ms);

    log.clear();
    // 同一条规矩：起算点取在注册之前，30ms 才是从这一刻起的绝对时刻
    const auto before_future = std::chrono::steady_clock::now();
    loop.call_at(before_future + 30ms, future); // 还没到
    CHECK(loop.pending(future));

    loop.run_until_complete();
    const auto waited = std::chrono::steady_clock::now() - before_future;

    CHECK(log == std::vector<HandleId>{future.id});
    CHECK(waited >= 28ms); // 提前醒就说明绝对时刻没被当真
}

TEST_CASE("call_at 同一时刻注册多个：全都会跑到，按 id 稳定裁决", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;
    Recorder a{log};
    Recorder b{log};
    Recorder c{log};

    // 三条记录的 TimePoint 完全相同，比较会落到 HandleInfo::operator<=>
    // 的 id 分支上 —— 顺序必须是确定的，不能靠指针地址
    const auto deadline = std::chrono::steady_clock::now() + 20ms;
    loop.call_at(deadline, a);
    loop.call_at(deadline, b);
    loop.call_at(deadline, c);

    CHECK(loop.pending(a));
    CHECK(loop.pending(b));
    CHECK(loop.pending(c));

    loop.run_until_complete();

    REQUIRE(log.size() == 3);
    CHECK(log[0] == a.id);
    CHECK(log[1] == b.id);
    CHECK(log[2] == c.id);
}

TEST_CASE("stopped 覆盖待办表的三种形态：就绪 / 定时 / 等事件", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;

    // 一进来就该是空的：这条同时盯着"上一个用例有没有留下残留记录"
    REQUIRE(loop.stopped());

    // 1) 就绪队列里的算待办
    Recorder ready{log};
    loop.call_soon(ready);
    CHECK_FALSE(loop.stopped());

    loop.run_until_complete();
    CHECK(loop.stopped()); // 跑完 → 记录被销掉，又空了
    REQUIRE(log == std::vector<HandleId>{ready.id});

    // 2) 还没到点的定时器算待办，摘掉立刻回到空
    Recorder timed{log};
    loop.call_after(1s, timed);
    CHECK_FALSE(loop.stopped());

    loop.cancel(timed);
    CHECK(loop.stopped());
    CHECK(log == std::vector<HandleId>{ready.id}); // 被取消的没跑

    // 3) 挂在 epoll 上等事件的算待办（内核侧的注册也要能被销干净）
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const FdGuard read_end{fds[0]};
    const FdGuard write_end{fds[1]};

    Observed out;
    Coro coro = await_readable(loop, fds[0], out);
    CHECK(loop.pending(coro.promise()));
    CHECK_FALSE(loop.stopped());

    loop.cancel(coro.promise());
    loop.run_until_complete();

    CHECK(out.resumed);
    CHECK(coro.finished());
    CHECK(loop.stopped()); // 没有残留的 fd 注册，否则这里会是假
}

TEST_CASE("cancel 摘除尚未到期的定时任务", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;
    Recorder handle{log};

    loop.call_after(1s, handle);
    REQUIRE(loop.pending(handle));

    loop.cancel(handle);
    CHECK(handle.cancelled);
    CHECK_FALSE(loop.pending(handle));

    // 定时器确实被摘掉了：循环应当立即返回，而不是等满 1 秒
    const auto begin = std::chrono::steady_clock::now();
    loop.run_until_complete();
    CHECK(std::chrono::steady_clock::now() - begin < 500ms);
    CHECK(log.empty());
}

TEST_CASE("cancel 阻止已进就绪队列的 handle 执行", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;
    Recorder handle{log};

    loop.call_soon(handle);
    loop.cancel(handle);
    CHECK(handle.cancelled);
    CHECK_FALSE(loop.pending(handle));

    loop.run_until_complete();

    CHECK(log.empty());
}

TEST_CASE("cancel 对未调度的 handle 只置标志", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;
    Recorder handle{log};

    REQUIRE_FALSE(loop.pending(handle));
    loop.cancel(handle);
    CHECK(handle.cancelled);

    loop.run_until_complete();
    CHECK(log.empty());
}

TEST_CASE("重复调度是幂等的，cancel 依然生效", "[event_loop]") {
    auto &loop = EventLoop::loop();
    std::vector<HandleId> log;

    SECTION("重复 call_soon 只执行一次") {
        Recorder handle{log};
        loop.call_soon(handle);
        loop.call_soon(handle);

        loop.run_until_complete();

        REQUIRE(log.size() == 1);
        CHECK(log[0] == handle.id);
    }

    SECTION("重复 call_after 只保留第一次的到期时间") {
        Recorder handle{log};
        loop.call_after(10ms, handle);
        loop.call_after(1s, handle);

        const auto begin = std::chrono::steady_clock::now();
        loop.run_until_complete();
        // 第二次 call_after 被忽略，所以不会等满 1 秒
        CHECK(std::chrono::steady_clock::now() - begin < 500ms);

        REQUIRE(log.size() == 1);
    }

    SECTION("重复 call_soon 后再 cancel，不执行") {
        Recorder handle{log};
        loop.call_soon(handle);
        loop.call_soon(handle);
        loop.cancel(handle);

        loop.run_until_complete();

        CHECK(log.empty());
    }

    SECTION("重复 call_after 后再 cancel，不执行") {
        Recorder handle{log};
        loop.call_after(50ms, handle);
        loop.call_after(80ms, handle);
        loop.cancel(handle);

        const auto begin = std::chrono::steady_clock::now();
        loop.run_until_complete();
        // 定时器已被摘除，不应等到 80ms
        CHECK(std::chrono::steady_clock::now() - begin < 500ms);

        CHECK(log.empty());
    }

    SECTION("先 call_soon 再 call_at，定时器被忽略") {
        Recorder handle{log};
        loop.call_soon(handle);
        loop.call_after(1s, handle);

        const auto begin = std::chrono::steady_clock::now();
        loop.run_until_complete();
        CHECK(std::chrono::steady_clock::now() - begin < 500ms);

        REQUIRE(log.size() == 1);
    }
}

TEST_CASE("wait_event 的 awaiter 不就绪且默认取值为成功", "[event_loop]") {
    auto &loop = EventLoop::loop();
    // EventAwaiter 是 EventLoop 的私有嵌套类型，这里用 auto 承接
    auto awaiter = loop.wait_event(Event{.fd = -1});

    CHECK_FALSE(awaiter.await_ready());

    const auto exp = awaiter.await_resume();
    CHECK(exp.has_value());
}

TEST_CASE("协程等待可读事件后被事件循环恢复", "[event_loop]") {
    auto &loop = EventLoop::loop();

    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const FdGuard read_end{fds[0]};
    const FdGuard write_end{fds[1]};

    Observed out;
    Coro coro = await_readable(loop, fds[0], out);

    // initial_suspend 是 suspend_never，所以协程体已经跑到 co_await 并挂起
    CHECK_FALSE(out.resumed);
    CHECK_FALSE(coro.finished());

    REQUIRE(::write(fds[1], "x", 1) == 1);

    loop.run_until_complete();

    CHECK(out.resumed);
    CHECK(out.ok);
    CHECK(coro.finished());
}

// 挂起中被取消：注销 fd，把结果置为 Cancelled 后立刻重新入队，让协程以
// "已取消"的形式恢复、有机会协作收尾。注意这里 **不需要** 往管道写数据，
// 也不需要等 epoll 事件 —— 取消必须是即时的；同时 fd 必须真的注销掉，
// 否则 stopped() 永远为假，run_until_complete() 会死锁在 poll(-1)。
TEST_CASE("cancel 挂起中的 handle：注销 fd 并以 Cancelled 结果恢复",
          "[event_loop]") {
    auto &loop = EventLoop::loop();

    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const FdGuard read_end{fds[0]};
    const FdGuard write_end{fds[1]};

    Observed out;
    Coro coro = await_readable(loop, fds[0], out);

    REQUIRE(loop.pending(coro.promise()));

    loop.cancel(coro.promise());
    // 已被重新入队，等待恢复
    CHECK(loop.pending(coro.promise()));
    CHECK(coro.promise().cancelled);

    const auto begin = std::chrono::steady_clock::now();
    loop.run_until_complete();
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    CHECK(elapsed < 500ms);
    CHECK(out.resumed);
    CHECK_FALSE(out.ok);
    CHECK(out.error == Error::Cancelled);
    CHECK(coro.finished());
}

TEST_CASE("Epoller 注册、触发、注销", "[event_loop]") {
    Epoller epoller;

    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const FdGuard read_end{fds[0]};
    const FdGuard write_end{fds[1]};

    Event event{.fd = fds[0], .flags = Event::kEventRead};

    REQUIRE(epoller.register_event(event).has_value());

    REQUIRE(::write(fds[1], "x", 1) == 1);

    const auto ready = epoller.poll(1000);
    REQUIRE(ready.has_value());
    REQUIRE(ready->size() == 1);
    // poll 返回的就是就绪的 fd
    CHECK((*ready)[0] == fds[0]);

    REQUIRE(epoller.unregister_event(event).has_value());
    // 再 DEL 一次会拿到 ENOENT，说明注册确实被摘掉了
    const auto again = epoller.unregister_event(event);
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error() == Error::InvalidOperation);
}

TEST_CASE("Epoller 对未注册的 fd 修改会返回错误", "[event_loop]") {
    Epoller epoller;

    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const FdGuard read_end{fds[0]};
    const FdGuard write_end{fds[1]};

    Event event{.fd = fds[0], .flags = Event::kEventRead};

    // 没 ADD 就 MOD，epoll_ctl 会返回 ENOENT，被映射成 InvalidOperation
    const auto exp = epoller.modify_event(event);
    REQUIRE_FALSE(exp.has_value());
    CHECK(exp.error() == Error::InvalidOperation);
}
