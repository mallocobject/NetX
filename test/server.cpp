// netx/net/server.hpp 的测试。
//
// 重点：
//   1. 链式接口经 deducing this 返回的确实是**派生类**引用 —— 这正是原来
//      CRTP 干的活，现在由显式对象参数推导
//   2. 配置错误一律折进 sticky_error_，不抛异常、不静默忽略
//   3. start() 是阻塞的（server_loop 常驻），测试里不驱动它

#include "netx/net/server.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

using namespace std::chrono_literals;

using netx::core::Expected;
using netx::core::Task;
using netx::core::details::Error;
using netx::core::details::make_error_code;
using netx::net::details::Address;
using netx::net::details::Server;
using netx::net::details::Stream;

// Socket 是命名空间（一组自由函数），不是类
namespace Sock = netx::net::details::Socket;

namespace {

class TestServer : public Server {
  public:
    TestServer(Stream &&stream, int idle1, int idle2)
        : Server(std::move(stream), idle1, idle2) {
    }

    // 派生类必须提供这个 —— 基类没有转发版本，漏写就是编译错误，
    // 而不是 CRTP 那种静默的无限递归
    Task<Expected<>> handle_client(int fd1, int fd2) {
        (void)fd1;
        (void)fd2;
        co_return {};
    }

    const std::error_code &sticky() const {
        return sticky_error_;
    }

    size_t configured_loops() const {
        return loop_count_;
    }

    const Address &local_addr() const {
        return stream_.sock_addr;
    }

    int listen_fd() const {
        return stream_.read_fd;
    }

    size_t connection_cap() const {
        return max_connections_;
    }

    // has_timeout() 是基类的 protected 成员
    bool timeout_is_set() const {
        return has_timeout();
    }

    std::chrono::nanoseconds timeout_value() const {
        return timeout_;
    }

    // 把受保护的容量门控露出来给用例断言
    bool try_slot() {
        return try_acquire_slot();
    }

    void close_connection() {
        release_connection();
    }
};

/// 建一个尚未绑定的监听 Stream —— 绑定交给 Server::listen() 做，
/// 否则这里绑一次、listen() 再绑一次会得到 EINVAL
std::optional<Stream> make_listen_stream() {
    auto fd = Sock::socket();
    if (!fd) {
        return std::nullopt;
    }
    if (auto exp = Sock::set_reuse_addr(*fd); !exp) {
        return std::nullopt;
    }

    auto stream = Stream::create(*fd);
    if (!stream) {
        return std::nullopt;
    }
    return std::move(*stream);
}

} // namespace

TEST_CASE("链式接口返回派生类引用（deducing this 取代 CRTP）",
          "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    // 关键：self 推导成 TestServer，所以返回的引用就是 TestServer&。
    // CRTP 那版靠 static_cast<Derived*>(this) 才拿得到同样的效果。
    auto &same = server.listen(Address{0, false}).loop(4).timeout(3s);
    static_assert(std::is_same_v<decltype(&same), TestServer *>,
                  "链式接口必须返回派生类引用");

    CHECK(&same == &server);
    CHECK(server.configured_loops() == 4);
}

TEST_CASE("listen 成功后记下本端地址", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    CHECK(server.local_addr().port() == 0); // 还没 listen

    server.listen(Address{0, false});
    REQUIRE_FALSE(server.sticky());
    CHECK(server.local_addr().port() != 0); // 内核分配的端口
    CHECK(server.local_addr().ip() == "0.0.0.0");
}

TEST_CASE("loop(0) 记进 sticky_error_ 而不是静默忽略", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    CHECK_FALSE(server.sticky());

    server.loop(0);
    CHECK(server.sticky() == make_error_code(Error::InvalidOperation));
    CHECK(server.configured_loops() == 1); // 非法值没被采纳

    // 已经挂上错误之后，后续配置不再生效（也不再被覆盖）
    server.loop(8);
    CHECK(server.configured_loops() == 1);
    CHECK(server.sticky() == make_error_code(Error::InvalidOperation));
}

TEST_CASE("bind 失败时不会走到 listen —— and_then 的短路由结构保证",
          "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    // 先自己把 fd 绑上，让 Server::listen 里的 bind 必然失败
    REQUIRE(Sock::bind(server.listen_fd(), Address{0, false}).has_value());

    server.listen(Address{0, false});
    REQUIRE(server.sticky());

    // 关键断言：listen() 系统调用没被执行过。
    // 三步各自 early-return 的写法也满足这点，但那是靠人写对；
    // and_then 是链断了后面就不会跑。
    int accepting = 0;
    socklen_t len = sizeof(accepting);
    REQUIRE(
        ::getsockopt(
            server.listen_fd(), SOL_SOCKET, SO_ACCEPTCONN, &accepting, &len) ==
        0);
    CHECK(accepting == 0);
}

TEST_CASE("析构会关掉构造函数传入的预留 fd", "[net][server]") {
    int fds[2] = {-1, -1};
    REQUIRE(::pipe(fds) == 0);

    {
        auto stream = make_listen_stream();
        REQUIRE(stream.has_value());
        // 预留 fd 由调用方预置时，所有权随之交给 Server
        TestServer server{std::move(*stream), fds[0], fds[1]};
        REQUIRE(::fcntl(fds[0], F_GETFD) != -1); // 还在
    }

    // 析构应当把它们都关掉 —— 少了这一步就是 fd 泄漏
    CHECK(::fcntl(fds[0], F_GETFD) == -1);
    CHECK(::fcntl(fds[1], F_GETFD) == -1);
}

TEST_CASE("max_connections 可以设置并参与链式调用", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    auto &same = server.max_connections(100).listen(Address{0, false}).loop(2);
    static_assert(std::is_same_v<decltype(&same), TestServer *>,
                  "新加的配置接口也必须返回派生类引用");

    CHECK(&same == &server);
    CHECK(server.connection_cap() == 100);
    CHECK(server.configured_loops() == 2);
    CHECK_FALSE(server.sticky());
}

TEST_CASE("默认不设超时，设了才算设", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    // 默认值是"不超时"的哨兵，HTTP 层靠它决定要不要挂定时器赛跑
    CHECK_FALSE(server.timeout_is_set());

    server.timeout(std::chrono::seconds(3));
    CHECK(server.timeout_is_set());
    CHECK(server.timeout_value() == std::chrono::seconds(3));
}

TEST_CASE("默认不设上限", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    CHECK(server.connection_cap() == std::numeric_limits<size_t>::max());
}

TEST_CASE("有空余时正常接纳，满了才拒绝，归还后恢复", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};
    server.max_connections(2);

    // 当前连接数 0 < 上限 2：还剩两个名额，前两条都正常接纳
    CHECK(server.try_slot());
    CHECK(server.try_slot());

    // 当前连接数已经等于上限：第三条被拒
    CHECK_FALSE(server.try_slot());

    // 关掉一条连接，名额还回来 —— 又变成"有空余"，可以继续接纳
    server.close_connection();
    CHECK(server.try_slot());

    // 取回来之后再次占满
    CHECK_FALSE(server.try_slot());
}

TEST_CASE("不设上限时永远接纳，归还也是空操作", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    // 没设上限就没建信号量，门控恒为真
    bool all_accepted = true;
    for (int i = 0; i < 10000; ++i) {
        all_accepted = all_accepted && server.try_slot();
    }
    CHECK(all_accepted);

    CHECK_NOTHROW(server.close_connection());
}

TEST_CASE("max_connections(0) 表示不限", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    server.max_connections(0);

    // 0 不是错误，是"不限"：保持哨兵值，也不建信号量
    CHECK_FALSE(server.sticky());
    CHECK(server.connection_cap() == std::numeric_limits<size_t>::max());

    // 门控恒为真 —— 不限时连取一万次都该成功
    bool all = true;
    for (int i = 0; i < 10000; ++i) {
        all = all && server.try_slot();
    }
    CHECK(all);

    // 先设了上限再传 0：0 只是不动它，不会把已有上限抹掉
    auto stream2 = make_listen_stream();
    REQUIRE(stream2.has_value());
    TestServer limited{std::move(*stream2), -1, -1};
    limited.max_connections(2);
    CHECK(limited.connection_cap() == 2);
    limited.max_connections(0);
    CHECK(limited.connection_cap() == 2);
}

TEST_CASE("非法 IP 折进 sticky_error_ 而不是抛异常", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    // Address 解析失败会抛 std::system_error；listen 要把它折进错误通道
    CHECK_NOTHROW(server.listen(std::string{"not-an-ip"}, 8080));
    CHECK(server.sticky() == std::make_error_code(std::errc::invalid_argument));
}

TEST_CASE("已经挂了错误时后续配置被跳过", "[net][server]") {
    auto stream = make_listen_stream();
    REQUIRE(stream.has_value());
    TestServer server{std::move(*stream), -1, -1};

    server.timeout(0s); // 合法，不挂错误
    CHECK_FALSE(server.sticky());

    server.listen(std::string{"999.1.1.1"}, 80); // 挂上错误
    REQUIRE(server.sticky());

    // sticky 之后 listen 直接返回，不再尝试绑定
    server.listen(Address{0, false});
    CHECK(server.local_addr().port() == 0); // 端口仍是 0，说明没绑
}
