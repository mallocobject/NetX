// netx/net/stream.hpp 的测试。
//
// 用 socketpair 造一对真实 fd 跑通读写；协程用 async_main 驱动。
// 重点覆盖：
//   1. 写出的数据能从对端原样读到
//   2. 对端写入的数据能被 read() 读进 read_buf
//   3. 对端关闭时 read() 报 BrokenPipe（而不是当成读到 0 字节）
//   4. 超过内核缓冲的写入走 write_buf 积压 + 等可写事件，数据不丢不乱
//   5. close() 可重复调用；读写共用同一个 fd 时不能关两次

#include "netx/net/stream.hpp"

#include "netx/core/async_main.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

using netx::core::async_main;
using netx::core::Expected;
using netx::core::Task;
using netx::core::details::Error;
using netx::core::details::make_error_code;
using netx::net::details::Stream;

namespace {

// 一对已连接的非阻塞 socket；析构时关闭仍归自己管的那些 fd
struct SocketPair {
    int a{-1};
    int b{-1};

    SocketPair() {
        int fds[2] = {-1, -1};
        if (::socketpair(
                AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) ==
            0) {
            a = fds[0];
            b = fds[1];
        }
    }

    ~SocketPair() {
        if (a != -1) {
            ::close(a);
        }
        if (b != -1) {
            ::close(b);
        }
    }

    SocketPair(const SocketPair &) = delete;
    SocketPair &operator=(const SocketPair &) = delete;

    // 把 a 的所有权交给 Stream 之后调用，避免双方都去 close
    void release_a() {
        a = -1;
    }
};

} // namespace

TEST_CASE("写出的数据能从对端原样读到", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();

    const std::string msg = "hello stream";
    const auto result = async_main(stream->write(msg));
    REQUIRE(result.has_value());

    char buf[64] = {};
    const ssize_t n = ::read(pair.b, buf, sizeof(buf));
    REQUIRE(n == static_cast<ssize_t>(msg.size()));
    CHECK(std::string(buf, static_cast<std::size_t>(n)) == msg);
}

TEST_CASE("对端写入的数据能被 read 读进 read_buf", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();

    const std::string msg = "hello back";
    REQUIRE(::write(pair.b, msg.data(), msg.size()) ==
            static_cast<ssize_t>(msg.size()));

    size_t got = 0;
    auto body = [&]() -> Task<Expected<>> {
        auto n = co_await stream->read();
        if (!n) {
            co_return std::unexpected{n.error()};
        }
        got = *n;
        co_return {};
    };

    const auto result = async_main(body());
    REQUIRE(result.has_value());
    CHECK(got == msg.size());
    CHECK(stream->read_buf.peek_string() == msg);
}

TEST_CASE("对端关闭时 read 报 BrokenPipe", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();

    // 关掉对端：read 会读到 EOF，而不是"0 字节、稍后重试"
    ::close(pair.b);
    pair.b = -1;

    const auto result = async_main(stream->read());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == make_error_code(Error::BrokenPipe));
}

TEST_CASE("超过内核缓冲的写入经 write_buf 排空且不丢不乱", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();

    // 1MB 远超 socketpair 的内核缓冲，必然触发 EAGAIN -> write_buf 积压
    // -> co_await 可写事件 -> 继续排空
    const std::string big(1 << 20, 'x');

    std::string received;
    received.reserve(big.size());
    std::atomic<bool> stop{false};

    std::thread drainer([&] {
        char buf[16 * 1024];
        while (received.size() < big.size()) {
            if (stop.load(std::memory_order_relaxed)) {
                return;
            }
            const ssize_t n = ::read(pair.b, buf, sizeof(buf));
            if (n > 0) {
                received.append(buf, static_cast<std::size_t>(n));
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                std::this_thread::yield();
            } else {
                return;
            }
        }
    });

    const auto result = async_main(stream->write(big));

    // write 返回只代表数据都交给了内核，不代表对端已经读走 —— 成功时让
    // drainer 一直读到收齐为止；只有写失败时才提前叫停，避免 join 卡死
    if (!result.has_value()) {
        stop.store(true, std::memory_order_relaxed);
    }
    drainer.join();

    REQUIRE(result.has_value());
    CHECK(received.size() == big.size());
    CHECK(received == big);                         // 内容与顺序都对
    CHECK(stream->write_buf.readable_bytes() == 0); // 缓冲已排空
}

TEST_CASE("write 传入空串是空操作", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();

    const auto result = async_main(stream->write(""));
    CHECK(result.has_value());
    CHECK(stream->write_buf.readable_bytes() == 0);
}

TEST_CASE("read 在没有数据时会挂起等可读事件", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();

    // 调用 read 时对端还没写 —— 第一次必然 EAGAIN，走到 co_await 可读事件
    // 那条路；数据稍后才到
    std::thread writer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const std::string msg = "late";
        (void)::write(pair.b, msg.data(), msg.size());
    });

    size_t got = 0;
    auto body = [&]() -> Task<Expected<>> {
        auto n = co_await stream->read();
        if (!n) {
            co_return std::unexpected{n.error()};
        }
        got = *n;
        co_return {};
    };

    const auto result = async_main(body());
    writer.join();

    REQUIRE(result.has_value());
    CHECK(got == 4);
    CHECK(stream->read_buf.peek_string() == "late");
}

TEST_CASE("shutdown 会先把缓冲里的积压写完再半关闭", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();

    // 模拟"上一次 write 中途被放弃"留下的积压：缓冲里有数据没发出去。
    // 同步版的 shutdown 会把它直接丢掉。
    const std::string pending = "pending-tail";
    stream->write_buf.append(pending);
    REQUIRE(stream->write_buf.readable_bytes() == pending.size());

    const auto result = async_main(stream->shutdown());
    REQUIRE(result.has_value());

    CHECK(stream->write_fd == -1);
    CHECK(stream->write_buf.readable_bytes() == 0); // 积压已排空

    // 对端应当先收到积压数据……
    std::string received(pending.size(), '\0');
    const ssize_t n = ::read(pair.b, received.data(), received.size());
    REQUIRE(n == static_cast<ssize_t>(pending.size()));
    CHECK(received == pending);

    // ……再读到 EOF（写端已半关闭）
    char extra = 0;
    CHECK(::read(pair.b, &extra, 1) == 0);
}

TEST_CASE("shutdown 可重复调用", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();

    CHECK(async_main(stream->shutdown()).has_value());
    CHECK(stream->write_fd == -1);

    // 第二次没有写端可关，应当是空操作而不是报错
    CHECK(async_main(stream->shutdown()).has_value());
}

TEST_CASE("close 可重复调用", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();

    CHECK(stream->close().has_value());
    CHECK(stream->read_fd == -1);
    CHECK(stream->write_fd == -1);

    CHECK(stream->close().has_value()); // 第二次是空操作，不该报 EBADF
}

TEST_CASE("读写共用同一个 fd 时 close 只关一次", "[net][stream]") {
    SocketPair pair;
    REQUIRE(pair.a != -1);

    // create(fd1, fd2) 传同一个 fd：read_fd == write_fd
    auto stream = Stream::create(pair.a, pair.a);
    REQUIRE(stream.has_value());
    pair.release_a();
    REQUIRE(stream->read_fd == stream->write_fd);

    // 关两次的话第二次会命中 EBADF（甚至关掉已被复用的新 fd）
    CHECK(stream->close().has_value());
}
