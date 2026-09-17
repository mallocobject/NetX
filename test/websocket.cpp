// netx/websocket 的测试。
//
// 这个模块此前完全没有覆盖。帧头里长度字段是网络序、掩码处理、控制帧约束
// 这些都不容易靠肉眼保证，所以按 RFC 6455 的线格式直接构造字节来验。

#include "netx/core/async_main.hpp"
#include "netx/net/stream.hpp"
#include "netx/websocket/connection.hpp"
#include "netx/websocket/frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using netx::core::async_main;
using netx::net::details::Stream;
using netx::websocket::details::Connection;
using netx::websocket::details::Frame;
using netx::websocket::details::is_control;
using netx::websocket::details::is_known_opcode;
using netx::websocket::details::Opcode;

namespace {

/// 一对非阻塞 socket；构造 Connection 时把 a 交给 Stream
struct Pair {
    int a{-1};
    int b{-1};

    Pair() {
        int fds[2] = {-1, -1};
        if (::socketpair(
                AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) ==
            0) {
            a = fds[0];
            b = fds[1];
        }
    }

    ~Pair() {
        if (a != -1) {
            ::close(a);
        }
        if (b != -1) {
            ::close(b);
        }
    }

    Pair(const Pair &) = delete;
    Pair &operator=(const Pair &) = delete;

    void release_a() {
        a = -1;
    }
};

void feed(int fd, std::string_view bytes) {
    const ssize_t n = ::write(fd, bytes.data(), bytes.size());
    REQUIRE(n == static_cast<ssize_t>(bytes.size()));
}

std::string drain(int fd) {
    std::string out;
    std::array<char, 4096> buf{};
    while (true) {
        pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
        if (::poll(&pfd, 1, 200) <= 0) {
            break;
        }
        const ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n <= 0) {
            break;
        }
        out.append(buf.data(), static_cast<size_t>(n));
    }
    return out;
}

/// 构造一个客户端帧（带掩码，符合 RFC：客户端发出的帧必须掩码）
std::string client_frame(Opcode op,
                         std::string_view payload,
                         bool fin = true,
                         const std::array<std::uint8_t, 4> &mask = {
                             1, 2, 3, 4}) {
    std::string out;
    out.push_back(
        static_cast<char>((fin ? 0x80 : 0x00) | static_cast<std::uint8_t>(op)));

    const size_t n = payload.size();
    if (n <= 125) {
        out.push_back(static_cast<char>(0x80 | n));
    } else if (n <= 0xFFFF) {
        out.push_back(static_cast<char>(0x80 | 126));
        Frame::append_be(out, n, 2);
    } else {
        out.push_back(static_cast<char>(0x80 | 127));
        Frame::append_be(out, n, 8);
    }

    out.append(reinterpret_cast<const char *>(mask.data()), 4);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^
                                        mask[i % 4]));
    }
    return out;
}

} // namespace

// ------------------------------------------------------------------ Frame

TEST_CASE("apply_mask 是可逆的，且对任意长度都对", "[websocket][frame]") {
    const std::array<std::uint8_t, 4> mask{0x12, 0x34, 0x56, 0x78};

    // 长度覆盖 4 的整数倍和余数两种情形（展开循环的尾部处理）
    for (size_t n : {0U, 1U, 2U, 3U, 4U, 5U, 7U, 8U, 100U}) {
        const std::string original(n, 'a');
        for (size_t i = 0; i < n; ++i) {
            const_cast<char &>(original[i]) = static_cast<char>('a' + (i % 26));
        }

        std::string masked = original;
        Frame::apply_mask(&masked, mask.data());
        if (n > 0) {
            CHECK(masked != original);
        }

        Frame::apply_mask(&masked, mask.data());
        CHECK(masked == original);
    }
}

TEST_CASE("read_be / append_be 走的是网络序", "[websocket][frame]") {
    std::string out;
    Frame::append_be(out, 0x0102, 2);
    REQUIRE(out.size() == 2);
    CHECK(static_cast<std::uint8_t>(out[0]) == 0x01); // 高位在前
    CHECK(static_cast<std::uint8_t>(out[1]) == 0x02);
    CHECK(Frame::read_be(out.data(), 2) == 0x0102);

    out.clear();
    Frame::append_be(out, 0x0102030405060708ULL, 8);
    REQUIRE(out.size() == 8);
    CHECK(static_cast<std::uint8_t>(out[0]) == 0x01);
    CHECK(static_cast<std::uint8_t>(out[7]) == 0x08);
    CHECK(Frame::read_be(out.data(), 8) == 0x0102030405060708ULL);
}

TEST_CASE("控制帧与保留 opcode 的判定", "[websocket][frame]") {
    CHECK(is_control(Opcode::kClose));
    CHECK(is_control(Opcode::kPing));
    CHECK(is_control(Opcode::kPong));
    CHECK_FALSE(is_control(Opcode::kText));
    CHECK_FALSE(is_control(Opcode::kBinary));
    CHECK_FALSE(is_control(Opcode::kContinuation));

    CHECK(is_known_opcode(0x0));
    CHECK(is_known_opcode(0x1));
    CHECK(is_known_opcode(0x2));
    CHECK(is_known_opcode(0x8));
    CHECK(is_known_opcode(0x9));
    CHECK(is_known_opcode(0xA));
    CHECK_FALSE(is_known_opcode(0x3)); // 保留
    CHECK_FALSE(is_known_opcode(0xB)); // 保留
}

// --------------------------------------------------------------- receive

TEST_CASE("收到短文本帧并解出掩码后的载荷", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};
    feed(pair.b, client_frame(Opcode::kText, "hello ws"));

    const auto frame = async_main(conn.receive());
    REQUIRE(frame.has_value());
    CHECK(frame->fin);
    CHECK(frame->opcode == Opcode::kText);
    CHECK(frame->payload == "hello ws");
}

TEST_CASE("126 扩展长度按网络序解析", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};

    // 长度 258 = 0x0102。按主机序读会得到 513（小端机上 0x0201），
    // 于是要么解出错长度、要么一直等数据。这里额外补 255 个字节，
    // 让错误的实现也能读完并落在断言上，而不是把用例挂住。
    std::string payload(258, 'A');
    std::string raw = client_frame(Opcode::kBinary, payload);
    raw.append(255, 'Z');
    feed(pair.b, raw);

    const auto frame = async_main(conn.receive());
    REQUIRE(frame.has_value());
    CHECK(frame->opcode == Opcode::kBinary);
    CHECK(frame->payload.size() == 258);
    CHECK(frame->payload == payload);
}

TEST_CASE("未掩码的帧也能收（服务端到服务端的情形）",
          "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};

    std::string raw;
    raw.push_back(static_cast<char>(0x80 | 0x01)); // fin + text
    raw.push_back(static_cast<char>(3));           // 无掩码位
    raw += "raw";
    feed(pair.b, raw);

    const auto frame = async_main(conn.receive());
    REQUIRE(frame.has_value());
    CHECK(frame->payload == "raw");
}

TEST_CASE("分片续帧的 fin 与 opcode 原样带出", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};
    feed(pair.b, client_frame(Opcode::kContinuation, "part2", /*fin=*/false));

    const auto frame = async_main(conn.receive());
    REQUIRE(frame.has_value());
    CHECK_FALSE(frame->fin);
    CHECK(frame->opcode == Opcode::kContinuation);
    CHECK(frame->payload == "part2");
}

TEST_CASE("RSV 位非零的帧被拒", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};

    std::string raw;
    raw.push_back(static_cast<char>(0x80 | 0x40 | 0x01)); // RSV1 置位
    raw.push_back(static_cast<char>(0x80 | 1));
    raw.append(4, '\1');
    raw += 'x';
    feed(pair.b, raw);

    const auto frame = async_main(conn.receive());
    CHECK_FALSE(frame.has_value());
}

TEST_CASE("保留 opcode 的帧被拒", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};

    std::string raw;
    raw.push_back(static_cast<char>(0x80 | 0x03)); // 0x3 是保留值
    raw.push_back(static_cast<char>(0x80 | 0));
    raw.append(4, '\1');
    feed(pair.b, raw);

    const auto frame = async_main(conn.receive());
    CHECK_FALSE(frame.has_value());
}

TEST_CASE("超过上限的载荷长度被拒，不会去等数据", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};

    // 127 分支声明 64GB：必须立刻拒绝，而不是挂在那儿等 64GB 到齐
    std::string raw;
    raw.push_back(static_cast<char>(0x80 | 0x02)); // fin + binary
    raw.push_back(static_cast<char>(0x80 | 127));
    Frame::append_be(raw, 64ULL * 1024 * 1024 * 1024, 8);
    raw.append(4, '\1');
    feed(pair.b, raw);

    const auto frame = async_main(conn.receive());
    CHECK_FALSE(frame.has_value());
}

TEST_CASE("分片的控制帧被拒", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};
    feed(pair.b, client_frame(Opcode::kPing, "p", /*fin=*/false));

    const auto frame = async_main(conn.receive());
    CHECK_FALSE(frame.has_value());
}

// ------------------------------------------------------------------ send

TEST_CASE("send_text 产生不掩码的文本帧", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};
    const auto sent = async_main(conn.send_text("hi"));
    REQUIRE(sent.has_value());

    const std::string wire = drain(pair.b);
    REQUIRE(wire.size() == 4);
    CHECK(static_cast<std::uint8_t>(wire[0]) == 0x81); // fin + text
    CHECK(static_cast<std::uint8_t>(wire[1]) == 2);    // 服务端不发掩码
    CHECK(wire.substr(2) == "hi");
}

TEST_CASE("send_* 各自的 opcode 正确", "[websocket][connection]") {
    const std::pair<netx::core::Task<netx::core::Expected<>> (*)(
                        Connection &, const std::string &),
                    std::uint8_t>
        cases[] = {
            {[](Connection &c, const std::string &s) { return c.send_text(s); },
             0x1},
            {[](Connection &c, const std::string &s) {
                 return c.send_binary(s);
             },
             0x2},
            {[](Connection &c, const std::string &s) {
                 return c.send_close(s);
             },
             0x8},
            {[](Connection &c, const std::string &s) { return c.send_ping(s); },
             0x9},
            {[](Connection &c, const std::string &s) { return c.send_pong(s); },
             0xA},
        };

    for (const auto &[send, opcode] : cases) {
        Pair pair;
        REQUIRE(pair.a != -1);
        auto stream = Stream::create(pair.a, ::dup(pair.a));
        REQUIRE(stream.has_value());
        pair.release_a();

        Connection conn{*stream};
        REQUIRE(async_main(send(conn, "p")).has_value());

        const std::string wire = drain(pair.b);
        REQUIRE(wire.size() == 3);
        CHECK(static_cast<std::uint8_t>(wire[0]) == (0x80 | opcode));
        CHECK(static_cast<std::uint8_t>(wire[1]) == 1);
        CHECK(wire[2] == 'p');
    }
}

TEST_CASE("超过 125 字节的载荷走 126 扩展长度", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};
    const std::string payload(300, 'z');
    REQUIRE(async_main(conn.send_binary(payload)).has_value());

    const std::string wire = drain(pair.b);
    REQUIRE(wire.size() == 300 + 4);
    CHECK(static_cast<std::uint8_t>(wire[0]) == 0x82);
    CHECK(static_cast<std::uint8_t>(wire[1]) == 126);
    CHECK(static_cast<std::uint8_t>(wire[2]) == 0x01); // 300 = 0x012C，网络序
    CHECK(static_cast<std::uint8_t>(wire[3]) == 0x2C);
    CHECK(wire.substr(4) == payload);
}

TEST_CASE("载荷超过 125 字节的控制帧被拒", "[websocket][connection]") {
    Pair pair;
    REQUIRE(pair.a != -1);
    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    Connection conn{*stream};
    const auto sent = async_main(conn.send_ping(std::string(200, 'x')));
    CHECK_FALSE(sent.has_value());
}
