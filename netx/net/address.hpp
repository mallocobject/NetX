#pragma once

#include "netx/net/endian.hpp"
#include <array>
#include <bit>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <system_error>
#include <utility>

namespace netx::net::details {

// 解析点分十进制 IPv4。语义对齐 inet_pton(AF_INET)：必须恰好 4 段、
// 每段 1..3 位十进制、不接受前导零（"01" 非法）、不接受空白或多余字符。
inline std::optional<std::array<std::byte, 4>>
parse_ipv4(std::string_view text) noexcept {
    std::array<std::byte, 4> out{};

    std::size_t pos = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto dot = text.find('.', pos);
        const bool is_last = (i == 3);

        // 前 3 段后面必须跟分隔点，第 4 段后面必须没有
        if (is_last ? (dot != std::string_view::npos)
                    : (dot == std::string_view::npos)) {
            return std::nullopt;
        }

        const auto part =
            is_last ? text.substr(pos) : text.substr(pos, dot - pos);
        if (part.empty() || part.size() > 3) {
            return std::nullopt;
        }
        if (part.size() > 1 && part.front() == '0') {
            return std::nullopt; // 前导零，inet_pton 同样拒绝
        }

        unsigned value = 0;
        const auto *first = part.data();
        const auto *last = first + part.size();
        // from_chars 不跳空白、不接受正负号，正好和 inet_pton 一致
        const auto [ptr, ec] = std::from_chars(first, last, value);
        if (ec != std::errc{} || ptr != last || value > 255) {
            return std::nullopt;
        }

        out[i] = static_cast<std::byte>(value);
        pos = dot + 1;
    }

    return out;
}

// IPv4 地址值类型。
class Address {
  public:
    using Octets = std::array<std::byte, 4>;

  private:
    sockaddr_in addr_{};

  public:
    constexpr Address() noexcept = default; // 0.0.0.0:0，sin_zero 全零

    // octets 按 a.b.c.d 的呈现顺序给；网络序即大端，直接按位重解释即可
    constexpr Address(Octets ip, std::uint16_t port) noexcept
        : addr_{.sin_family = AF_INET,
                .sin_port = host2be(port),
                .sin_addr = {.s_addr = std::bit_cast<std::uint32_t>(ip)},
                .sin_zero = {}} { // 显式清零，否则 -Wextra 报缺初始化
    }

    // 只给端口：默认绑回环，loopback_only=false 时是 INADDR_ANY
    explicit constexpr Address(std::uint16_t port,
                               bool loopback_only = true) noexcept
        : Address(loopback_only ? Octets{std::byte{127},
                                         std::byte{0},
                                         std::byte{0},
                                         std::byte{1}}
                                : Octets{},
                  port) {
    }

    // 解析失败抛 std::system_error(EINVAL)；不想碰异常就用 try_parse
    Address(std::string_view ip, std::uint16_t port) {
        auto parsed = parse_ipv4(ip);
        if (!parsed) {
            throw std::system_error(
                std::make_error_code(std::errc::invalid_argument),
                std::format("Address: 非法的 IPv4 地址 \"{}\"", ip));
        }
        addr_.sin_family = AF_INET;
        addr_.sin_port = host2be(port);
        addr_.sin_addr.s_addr = std::bit_cast<std::uint32_t>(*parsed);
    }

    static std::optional<Address> try_parse(std::string_view ip,
                                            std::uint16_t port) noexcept {
        if (auto parsed = parse_ipv4(ip)) {
            return Address{*parsed, port};
        }
        return std::nullopt;
    }

    // accept/recvfrom 之类拿到内核填好的 sockaddr_in 后回建 Address
    explicit constexpr Address(const sockaddr_in &sa) noexcept : addr_(sa) {
    }

    constexpr Octets octets() const noexcept {
        return std::bit_cast<Octets>(addr_.sin_addr.s_addr);
    }

    constexpr std::uint16_t port() const noexcept {
        return be2host(addr_.sin_port);
    }

    std::string ip() const {
        const auto o = octets();
        return std::format("{}.{}.{}.{}",
                           std::to_integer<unsigned>(o[0]),
                           std::to_integer<unsigned>(o[1]),
                           std::to_integer<unsigned>(o[2]),
                           std::to_integer<unsigned>(o[3]));
    }

    std::string to_formatted_string() const {
        return std::format("{}:{}", ip(), port());
    }

    // sin_zero 是填充字节，从外部 sockaddr_in 构造时可能是垃圾值，不能比
    friend constexpr bool operator==(const Address &lhs,
                                     const Address &rhs) noexcept {
        return lhs.addr_.sin_family == rhs.addr_.sin_family &&
               lhs.addr_.sin_port == rhs.addr_.sin_port &&
               lhs.addr_.sin_addr.s_addr == rhs.addr_.sin_addr.s_addr;
    }

    // ---------------- POSIX 边界：以下是本头唯一的平台依赖 ----------------

    // 需要脱离 Address 生命周期的独立副本时用这个
    constexpr sockaddr_in to_sockaddr_in() const noexcept {
        return addr_;
    }
};

// 让 sockaddr_in 只活在一次系统调用的栈帧里，和 Address 的生命周期解耦。
//
// const int rc = with_sockaddr(addr, [&](const sockaddr *sa, socklen_t len) {
//     return ::bind(fd, sa, len);
// });
template <typename Fn>
decltype(auto) with_sockaddr(const Address &addr, Fn &&fn)
    requires std::invocable<Fn, const struct sockaddr *, socklen_t>
{
    const sockaddr_in sa = addr.to_sockaddr_in();
    return std::forward<Fn>(fn)(reinterpret_cast<const struct sockaddr *>(&sa),
                                static_cast<socklen_t>(sizeof(sa)));
}

} // namespace netx::net::details
