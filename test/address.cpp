// netx/net/address.hpp 的测试。
//
// 点分十进制解析与格式化是自己用 std::from_chars / std::format 实现的，所以
// 这里把被替换掉的 inet_pton / inet_ntop 当作对手实现逐条对齐——它们正是
// 原来那批函数，接受/拒绝的行为必须一致。

#include "netx/net/address.hpp"

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>

using netx::net::details::Address;
using netx::net::details::be2host;
using netx::net::details::host2be;
using netx::net::details::parse_ipv4;
using netx::net::details::with_sockaddr;

// 存储用 sockaddr_in，构造与取值仍然全程可 constexpr
static_assert(Address{}.port() == 0);
static_assert(Address{
                  std::array<std::byte, 4>{
                      std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}},
                  8080}
                  .port() == 8080);
static_assert(Address{
                  std::array<std::byte, 4>{
                      std::byte{10}, std::byte{0}, std::byte{0}, std::byte{1}},
                  80}
                  .octets() ==
              std::array<std::byte, 4>{
                  std::byte{10}, std::byte{0}, std::byte{0}, std::byte{1}});

TEST_CASE("默认构造是 0.0.0.0:0", "[net][address]") {
    const Address a;
    CHECK(a.port() == 0);
    CHECK(a.ip() == "0.0.0.0");
    CHECK(a.to_formatted_string() == "0.0.0.0:0");
}

TEST_CASE("只给端口时回环与通配地址", "[net][address]") {
    const Address loopback{8080};
    CHECK(loopback.ip() == "127.0.0.1");
    CHECK(loopback.port() == 8080);

    const Address any{8080, false};
    CHECK(any.ip() == "0.0.0.0");

    const Address explicit_any{8080, /*loopback_only=*/false};
    CHECK(any == explicit_any);
    CHECK_FALSE(any == loopback);
}

TEST_CASE("从字符串构造并格式化回来", "[net][address]") {
    const Address a{"192.168.1.100", 65535};
    CHECK(a.ip() == "192.168.1.100");
    CHECK(a.port() == 65535);
    CHECK(a.to_formatted_string() == "192.168.1.100:65535");

    CHECK(a.octets() ==
          std::array<std::byte, 4>{
              std::byte{192}, std::byte{168}, std::byte{1}, std::byte{100}});
}

TEST_CASE("IPv4 解析与 inet_pton 逐条对齐", "[net][address]") {
    const char *cases[] = {
        // 合法
        "0.0.0.0",
        "127.0.0.1",
        "255.255.255.255",
        "1.2.3.4",
        "192.168.1.1",
        "10.0.0.255",
        // 非法：越界、段数不对、前导零、空白、符号、非数字、空段
        "256.1.1.1",
        "1.2.3",
        "1.2.3.4.5",
        "01.2.3.4",
        "1.2.3.04",
        " 1.2.3.4",
        "1.2.3.4 ",
        "1.2.3.4\n",
        "1.2.3.-1",
        "+1.2.3.4",
        "",
        "1..2.3",
        "a.b.c.d",
        "1.2.3.4x",
        "001.002.003.004",
        "...",
        "1.2.3.",
        ".1.2.3",
        "1.2.3.256",
    };

    for (const char *text : cases) {
        in_addr oracle{};
        const int rc = ::inet_pton(AF_INET, text, &oracle);
        const auto mine = parse_ipv4(text);

        INFO("输入 = \"" << text << "\"");
        REQUIRE(mine.has_value() == (rc == 1));
        if (rc == 1) {
            std::array<std::byte, 4> want{};
            std::memcpy(want.data(), &oracle, want.size());
            CHECK(*mine == want);
        }
    }
}

TEST_CASE("IPv4 格式化与 inet_ntop 一致", "[net][address]") {
    const std::array<std::array<std::byte, 4>, 9> addrs{{
        {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}},
        {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}},
        {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}},
        {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}},
        {std::byte{10}, std::byte{0}, std::byte{0}, std::byte{255}},
        {std::byte{192}, std::byte{168}, std::byte{1}, std::byte{1}},
        {std::byte{8}, std::byte{8}, std::byte{8}, std::byte{8}},
        {std::byte{100}, std::byte{64}, std::byte{0}, std::byte{7}},
        {std::byte{172}, std::byte{16}, std::byte{254}, std::byte{1}},
    }};

    for (const auto &octets : addrs) {
        in_addr raw{};
        std::memcpy(&raw, octets.data(), octets.size());
        char buf[INET_ADDRSTRLEN] = {};
        REQUIRE(::inet_ntop(AF_INET, &raw, buf, sizeof(buf)) != nullptr);

        const Address a{octets, 1234};
        INFO("期望 = " << buf);
        CHECK(a.ip() == buf);
    }
}

TEST_CASE("解析与格式化互为逆运算", "[net][address]") {
    const std::array<std::array<std::byte, 4>, 6> addrs{{
        {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}},
        {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}},
        {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}},
        {std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40}},
        {std::byte{1}, std::byte{0}, std::byte{0}, std::byte{1}},
        {std::byte{203}, std::byte{0}, std::byte{113}, std::byte{7}},
    }};

    for (const auto &octets : addrs) {
        const Address original{octets, 1234};
        const auto reparsed =
            Address::try_parse(original.ip(), original.port());

        REQUIRE(reparsed.has_value());
        CHECK(*reparsed == original);
        CHECK(reparsed->ip() == original.ip());
        CHECK(reparsed->octets() == octets);
    }
}

TEST_CASE("try_parse 不抛异常，构造失败抛 system_error", "[net][address]") {
    CHECK(Address::try_parse("1.2.3.4", 80).has_value());
    CHECK_FALSE(Address::try_parse("1.2.3.256", 80).has_value());
    CHECK_FALSE(Address::try_parse("nonsense", 80).has_value());

    CHECK_NOTHROW((Address{"1.2.3.4", 80}));
    CHECK_THROWS_AS((Address{"1.2.3.256", 80}), std::system_error);
    CHECK_THROWS_AS((Address{"", 80}), std::system_error);
}

TEST_CASE("sockaddr_in 桥接往返一致且端口是网络序", "[net][address]") {
    const Address original{"10.20.30.40", 51820};

    const sockaddr_in sa = original.to_sockaddr_in();
    CHECK(sa.sin_family == AF_INET);
    // 内核要求的网络字节序：和 htons 对齐
    CHECK(sa.sin_port == htons(51820));
    CHECK(sa.sin_port == host2be(std::uint16_t{51820}));

    const Address rebuilt{sa};
    CHECK(rebuilt == original);
    CHECK(rebuilt.ip() == "10.20.30.40");
    CHECK(rebuilt.port() == 51820);
}

TEST_CASE("端口边界值经 sockaddr 桥接保持不变", "[net][address]") {
    const std::uint16_t ports[] = {0, 1, 80, 8080, 65535};

    for (std::uint16_t port : ports) {
        const Address a{"203.0.113.7", port};
        const sockaddr_in sa = a.to_sockaddr_in();

        INFO("端口 = " << port);
        CHECK(sa.sin_port == htons(port));
        CHECK(Address{sa} == a);
        CHECK(Address{sa}.port() == port);
    }
}

TEST_CASE("to_sockaddr_in 返回独立副本", "[net][address]") {
    Address a{"172.16.254.1", 8443};

    sockaddr_in sa = a.to_sockaddr_in();
    CHECK(sa.sin_family == AF_INET);
    CHECK(sa.sin_port == htons(8443));

    // 改副本不影响原对象：边界上不再有"指针指回内部缓冲"的耦合
    sa.sin_port = htons(9999);
    CHECK(a.port() == 8443);
    CHECK(a.ip() == "172.16.254.1");
    CHECK(a == (Address{"172.16.254.1", 8443}));

    // 副本本身是完整可用的，回建后端口跟着副本走
    CHECK(Address{sa}.port() == 9999);
}

TEST_CASE("with_sockaddr 传出正确的长度与协议族", "[net][address]") {
    const Address a{"127.0.0.1", 9000};

    bool called = false;
    const int rc =
        with_sockaddr(a, [&](const struct sockaddr *sa, socklen_t len) {
            called = true;
            CHECK(len == sizeof(sockaddr_in));
            REQUIRE(sa != nullptr);
            CHECK(sa->sa_family == AF_INET);

            const auto *in = reinterpret_cast<const sockaddr_in *>(sa);
            CHECK(be2host(in->sin_port) == 9000);

            Address back{*in};
            CHECK(back == a);
            return 0;
        });

    CHECK(called);
    CHECK(rc == 0);
}
