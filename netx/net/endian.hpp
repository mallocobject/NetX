#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>

// 字节序转换全部走标准库：
//   std::endian     (C++20) 编译期判断本机字节序，用来消掉整个分支
//   std::byteswap   (C++23) 整数翻转
//   std::bit_cast   (C++20) 浮点按位重解释，取代 memcpy
// 因此不再需要 glibc 的 <endian.h>（换平台要改成 <libkern/OSByteOrder.h>
// 之类），而且整条链路都是 constexpr，可以直接放进 static_assert。
namespace netx::net::details {

template <typename T>
    requires((std::is_integral_v<T> && !std::is_same_v<T, bool>) ||
             std::is_floating_point_v<T>)
constexpr T host2be(T x) noexcept {
    if constexpr (sizeof(T) == 1) {
        return x; // 单字节没有字节序问题
    } else if constexpr (std::endian::native == std::endian::big) {
        return x; // 本机就是大端，什么都不用做
    } else if constexpr (std::is_integral_v<T>) {
        return std::byteswap(x);
    } else {
        static_assert(sizeof(T) == 4 || sizeof(T) == 8,
                      "host2be: 浮点只支持 4/8 字节");
        if constexpr (sizeof(T) == 4) {
            return std::bit_cast<T>(
                std::byteswap(std::bit_cast<std::uint32_t>(x)));
        } else {
            return std::bit_cast<T>(
                std::byteswap(std::bit_cast<std::uint64_t>(x)));
        }
    }
}

// 网络字节序就是大端，所以两个方向是同一个操作
template <typename T>
    requires((std::is_integral_v<T> && !std::is_same_v<T, bool>) ||
             std::is_floating_point_v<T>)
constexpr T be2host(T x) noexcept {
    return host2be(x);
}

} // namespace netx::net::details
