// netx/net/socket.hpp 的测试。
//
// 走真实内核：建 socket、bind、listen、非阻塞 connect、accept、收发、关闭。
// 重点盯住两个容易写错的不变量：
//   1. connect() 必须把 EINPROGRESS 当作"已发起"而非失败 —— 否则非阻塞套接字
//      上每一个连接都会被误报成错误
//   2. check_socket_error() 必须把非零 SO_ERROR 报成失败，而不是当成成功的值
//      —— 否则延迟的连接失败会被静默吞掉

#include "netx/net/socket.hpp"

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>

namespace sock = netx::net::details::Socket;
using netx::core::Expected;
using netx::net::details::Address;

namespace {

// 等 fd 上出现指定事件（单元测试里用超时兜底，避免挂死）
bool wait_event(int fd, short events, int timeout_ms = 2000) {
    pollfd p{fd, events, 0};
    const int rc = ::poll(&p, 1, timeout_ms);
    return rc > 0 && (p.revents & events) != 0;
}

// 占住一个 127.0.0.1 上的端口并返回它；调用方负责 close。
// 只 bind 不 listen，所以连过去必然被拒 —— 比"先探测再连接"少一个竞态。
int hold_free_port(std::uint16_t &port_out) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        return -1;
    }

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) == -1) {
        ::close(fd);
        return -1;
    }

    socklen_t len = sizeof(sa);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&sa), &len) == -1) {
        ::close(fd);
        return -1;
    }

    port_out = ntohs(sa.sin_port);
    return fd;
}

} // namespace

TEST_CASE("socket() 建出非阻塞且 CLOEXEC 的 fd", "[net][socket]") {
    const auto fd = sock::socket();
    REQUIRE(fd.has_value());

    const int flags = ::fcntl(*fd, F_GETFL, 0);
    REQUIRE(flags != -1);
    CHECK((flags & O_NONBLOCK) != 0);

    const int fd_flags = ::fcntl(*fd, F_GETFD, 0);
    REQUIRE(fd_flags != -1);
    CHECK((fd_flags & FD_CLOEXEC) != 0);

    CHECK(sock::close(*fd).has_value());
}

TEST_CASE("set_non_blocking 可以开关", "[net][socket]") {
    const auto fd = sock::socket();
    REQUIRE(fd.has_value());

    REQUIRE(sock::set_non_blocking(*fd, false).has_value());
    CHECK((::fcntl(*fd, F_GETFL, 0) & O_NONBLOCK) == 0);

    REQUIRE(sock::set_non_blocking(*fd, true).has_value());
    CHECK((::fcntl(*fd, F_GETFL, 0) & O_NONBLOCK) != 0);

    CHECK(sock::close(*fd).has_value());
}

TEST_CASE("套接字选项确实落到内核", "[net][socket]") {
    const auto fd = sock::socket();
    REQUIRE(fd.has_value());

    CHECK(sock::set_reuse_addr(*fd).has_value());
    CHECK(sock::set_keep_alive(*fd).has_value());
    CHECK(sock::set_no_delay(*fd).has_value());

    int optval = 0;
    socklen_t len = sizeof(optval);
    REQUIRE(::getsockopt(*fd, SOL_SOCKET, SO_KEEPALIVE, &optval, &len) == 0);
    CHECK(optval == 1);

    optval = 0;
    len = sizeof(optval);
    REQUIRE(::getsockopt(*fd, IPPROTO_TCP, TCP_NODELAY, &optval, &len) == 0);
    CHECK(optval == 1);

    CHECK(sock::close(*fd).has_value());
}

TEST_CASE("bind 端口 0 后能读回内核分配的地址", "[net][socket]") {
    const auto fd = sock::socket();
    REQUIRE(fd.has_value());
    REQUIRE(sock::set_reuse_addr(*fd).has_value());

    REQUIRE(sock::bind(*fd, Address{0, /*loopback_only=*/false}).has_value());
    REQUIRE(sock::listen(*fd).has_value());

    const auto bound = sock::get_socket_name(*fd);
    REQUIRE(bound.has_value());
    CHECK(bound->port() != 0);
    CHECK(bound->ip() == "0.0.0.0");

    CHECK(sock::close(*fd).has_value());
}

TEST_CASE("非阻塞 connect 不被误报为错误并能真正建连", "[net][socket]") {
    // ---- 监听端 ----
    const auto listen_fd = sock::socket();
    REQUIRE(listen_fd.has_value());
    REQUIRE(sock::set_reuse_addr(*listen_fd).has_value());
    REQUIRE(sock::bind(*listen_fd, Address{"127.0.0.1", 0}).has_value());
    REQUIRE(sock::listen(*listen_fd).has_value());

    const auto bound = sock::get_socket_name(*listen_fd);
    REQUIRE(bound.has_value());
    REQUIRE(bound->port() != 0);

    // ---- 连接端 ----
    const auto client_fd = sock::socket();
    REQUIRE(client_fd.has_value());

    // 修复点 1：非阻塞 connect 返回 EINPROGRESS 属于"已发起"，不是失败
    const Expected<> started = sock::connect(*client_fd, *bound);
    INFO("connect 结果: " << (started ? "成功" : started.error().message()));
    REQUIRE(started.has_value());

    REQUIRE(wait_event(*client_fd, POLLOUT));
    const Expected<> settled = sock::check_socket_error(*client_fd);
    INFO("SO_ERROR: " << (settled ? "0" : settled.error().message()));
    CHECK(settled.has_value());

    // ---- 服务端 accept ----
    REQUIRE(wait_event(*listen_fd, POLLIN));
    Address peer{};
    const auto conn_fd = sock::accept(*listen_fd, &peer);
    REQUIRE(conn_fd.has_value());
    CHECK(peer.ip() == "127.0.0.1");
    CHECK(peer.port() != 0);

    // ---- 两端互认 ----
    const auto local = sock::get_socket_name(*client_fd);
    REQUIRE(local.has_value());
    CHECK(local->port() == peer.port());

    const auto server_side_peer = sock::get_peer_name(*conn_fd);
    REQUIRE(server_side_peer.has_value());
    CHECK(server_side_peer->ip() == "127.0.0.1");
    CHECK(server_side_peer->port() == local->port());

    // ---- 收发 ----
    const std::string msg = "hello netx";
    REQUIRE(::send(*client_fd, msg.data(), msg.size(), 0) ==
            static_cast<ssize_t>(msg.size()));

    REQUIRE(wait_event(*conn_fd, POLLIN));
    char buf[64] = {};
    const ssize_t n = ::recv(*conn_fd, buf, sizeof(buf), 0);
    REQUIRE(n == static_cast<ssize_t>(msg.size()));
    CHECK(std::string(buf, static_cast<std::size_t>(n)) == msg);

    // ---- shutdown 后对端读到 EOF ----
    REQUIRE(sock::shutdown(*client_fd).has_value());
    REQUIRE(wait_event(*conn_fd, POLLIN));
    CHECK(::recv(*conn_fd, buf, sizeof(buf), 0) == 0);

    CHECK(sock::close(*conn_fd).has_value());
    CHECK(sock::close(*client_fd).has_value());
    CHECK(sock::close(*listen_fd).has_value());
}

TEST_CASE("延迟的连接失败经 check_socket_error 报出来", "[net][socket]") {
    // 占住端口但不 listen：连过去会被内核回 RST
    std::uint16_t port = 0;
    const int holder = hold_free_port(port);
    REQUIRE(holder != -1);
    REQUIRE(port != 0);

    const auto fd = sock::socket();
    REQUIRE(fd.has_value());

    // 发起阶段照样是"成功"（EINPROGRESS）
    const Expected<> started = sock::connect(*fd, Address{"127.0.0.1", port});
    REQUIRE(started.has_value());

    REQUIRE(wait_event(*fd, POLLOUT));

    // 修复点 2：以前这里返回的是 {error}，Expected 恒为真，失败被吞掉
    const Expected<> settled = sock::check_socket_error(*fd);
    INFO("SO_ERROR: " << (settled ? "成功（说明错误被吞了）"
                                  : settled.error().message()));
    REQUIRE_FALSE(settled.has_value());
    CHECK(settled.error() ==
          std::error_code(ECONNREFUSED, std::system_category()));

    CHECK(sock::close(*fd).has_value());
    CHECK(::close(holder) == 0);
}

TEST_CASE("没有待处理连接时 accept 返回 EAGAIN", "[net][socket]") {
    const auto listen_fd = sock::socket();
    REQUIRE(listen_fd.has_value());
    REQUIRE(sock::bind(*listen_fd, Address{"127.0.0.1", 0}).has_value());
    REQUIRE(sock::listen(*listen_fd).has_value());

    // 不传 peer_addr 也应当能调用（默认参数）
    const auto conn = sock::accept(*listen_fd);
    REQUIRE_FALSE(conn.has_value());
    CHECK(conn.error() == std::error_code(EAGAIN, std::system_category()));

    CHECK(sock::close(*listen_fd).has_value());
}

TEST_CASE("重复 close 会报 EBADF 并映射成 InvalidOperation", "[net][socket]") {
    const auto fd = sock::socket();
    REQUIRE(fd.has_value());
    REQUIRE(sock::close(*fd).has_value());

    const auto again = sock::close(*fd);
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error() == netx::core::details::make_error_code(
                               netx::core::details::Error::InvalidOperation));
}

TEST_CASE("对未连接的 fd 取对端地址会失败", "[net][socket]") {
    const auto fd = sock::socket();
    REQUIRE(fd.has_value());

    const auto peer = sock::get_peer_name(*fd);
    CHECK_FALSE(peer.has_value());
    CHECK(peer.error() == std::error_code(ENOTCONN, std::system_category()));

    CHECK(sock::close(*fd).has_value());
}
