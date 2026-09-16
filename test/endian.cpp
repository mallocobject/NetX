// netx/net/endian.hpp 的测试。
//
// 实现已换成纯标准库（std::endian / std::byteswap / std::bit_cast），所以这里
// 把被替换掉的 glibc <endian.h> 函数当作对手实现逐位对齐，并验证整条链路可
// constexpr —— 旧实现依赖 <endian.h> 的函数，做不到这一点。

#include "netx/net/endian.hpp"

#include <catch2/catch_test_macros.hpp>

#include <endian.h>

#include <bit>
#include <cstdint>
#include <cstring>

using netx::net::details::be2host;
using netx::net::details::host2be;

// 编译期就得算出来
static_assert(be2host(host2be(std::uint16_t{0x1234})) == 0x1234);
static_assert(be2host(host2be(std::uint32_t{0x11223344})) == 0x11223344);
static_assert(be2host(host2be(std::uint64_t{0xDEADBEEFCAFEBABE})) ==
              0xDEADBEEFCAFEBABE);
static_assert(be2host(host2be(1.5f)) == 1.5f);
static_assert(be2host(host2be(3.14159265358979)) == 3.14159265358979);

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
// 小端机上网络序就是把字节翻过来
static_assert(host2be(std::uint16_t{0x1234}) == 0x3412);
static_assert(host2be(std::uint32_t{0x11223344}) == 0x44332211);
static_assert(host2be(std::uint64_t{0x1122334455667788}) == 0x8877665544332211);
// 浮点同样按位翻转：1.5f = 0x3FC00000 -> 0x0000C03F
static_assert(host2be(1.5f) != 1.5f);
#endif

TEST_CASE("host2be / be2host 与 glibc 的 htobe* / be*toh 结果一致",
          "[net][endian]") {
    // 取一批分布均匀的值，逐位比对被替换掉的 POSIX 实现
    for (std::uint64_t v = 0; v < 1000; ++v) {
        const std::uint64_t x = v * 0x9E3779B97F4A7C15ull;

        const auto x16 = static_cast<std::uint16_t>(x);
        const auto x32 = static_cast<std::uint32_t>(x);

        CHECK(host2be(x16) == static_cast<std::uint16_t>(htobe16(x16)));
        CHECK(host2be(x32) == static_cast<std::uint32_t>(htobe32(x32)));
        CHECK(host2be(x) == htobe64(x));

        CHECK(be2host(x16) == static_cast<std::uint16_t>(be16toh(x16)));
        CHECK(be2host(x32) == static_cast<std::uint32_t>(be32toh(x32)));
        CHECK(be2host(x) == be64toh(x));
    }
}

TEST_CASE("16 位全量取值上往返恒等且与 htobe16 一致", "[net][endian]") {
    // 65536 个取值全部走一遍；断言合并成一条，避免 6 万次 CHECK 拖慢用例
    bool ok = true;
    for (unsigned v = 0; v <= 0xFFFF; ++v) {
        const auto x = static_cast<std::uint16_t>(v);
        ok = ok && (be2host(host2be(x)) == x) &&
             (host2be(x) == static_cast<std::uint16_t>(htobe16(x)));
    }
    CHECK(ok);
}

TEST_CASE("host2be 覆盖各宽度与有符号类型", "[net][endian]") {
    CHECK(host2be(std::uint8_t{0xAB}) == 0xAB); // 单字节不变
    CHECK(host2be(std::int8_t{-1}) == std::int8_t{-1});

    // 有符号类型按位模式搬运：结果应与无符号版本完全一致
    const std::int32_t neg = -2;
    std::uint32_t bits = 0;
    const auto flipped = host2be(neg);
    std::memcpy(&bits, &flipped, sizeof(bits));
    CHECK(bits == htobe32(static_cast<std::uint32_t>(neg)));

    CHECK(be2host(host2be(std::int64_t{-1234567890123})) == -1234567890123);
}

TEST_CASE("浮点转换往返无损", "[net][endian]") {
    for (float f : {0.0f, 1.0f, -1.0f, 1.5f, 3.14159f, 1e-30f, 1e30f}) {
        CHECK(be2host(host2be(f)) == f);
    }
    for (double d : {0.0, 1.0, -1.0, 1.5, 3.14159265358979, 1e-300, 1e300}) {
        CHECK(be2host(host2be(d)) == d);
    }
}

TEST_CASE("浮点转换是精确的按位翻转", "[net][endian]") {
    // 不能只验往返：往返恒等在"翻两次"或"压根没翻"时都成立，
    // 这里直接比对位模式，确认翻转确实发生且与 byteswap 等价
    const float f = 1.5f; // 0x3FC00000
    CHECK(std::bit_cast<std::uint32_t>(host2be(f)) ==
          std::byteswap(std::bit_cast<std::uint32_t>(f)));

    const double d = 1.5; // 0x3FF8000000000000
    CHECK(std::bit_cast<std::uint64_t>(host2be(d)) ==
          std::byteswap(std::bit_cast<std::uint64_t>(d)));

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    CHECK(std::bit_cast<std::uint32_t>(host2be(f)) == 0x0000C03Fu);
    CHECK(std::bit_cast<std::uint64_t>(host2be(d)) == 0x000000000000F83Full);
#endif
}
