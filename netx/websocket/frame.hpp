#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace netx::websocket::details {
enum class Opcode : std::uint8_t {
    kContinuation = 0x0,
    kText = 0x1,
    kBinary = 0x2,
    kClose = 0x8,
    kPing = 0x9,
    kPong = 0xA
};

/// 控制帧（0x8 起）不能分片，且载荷不超过 125 字节（RFC 6455 §5.5）
[[nodiscard]] constexpr bool is_control(Opcode op) noexcept {
    return (static_cast<std::uint8_t>(op) & 0x08) != 0;
}

[[nodiscard]] constexpr bool is_known_opcode(std::uint8_t raw) noexcept {
    switch (raw) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x8:
    case 0x9:
    case 0xA:
        return true;
    default:
        return false; // 0x3-0x7、0xB-0xF 都是保留值
    }
}

struct Frame {
    /// 就地按 4 字节掩码异或。
    /// 原实现用 data->at(i)，每个字节都带一次边界检查；掩码按 4 字节一轮
    /// 展开后循环体里不再有取模。
    static void apply_mask(std::string *data, const std::uint8_t mask[4]) {
        const size_t n = data->size();
        char *p = data->data();

        size_t i = 0;
        for (; i + 4 <= n; i += 4) {
            p[i + 0] ^= static_cast<char>(mask[0]);
            p[i + 1] ^= static_cast<char>(mask[1]);
            p[i + 2] ^= static_cast<char>(mask[2]);
            p[i + 3] ^= static_cast<char>(mask[3]);
        }
        for (size_t k = 0; i < n; ++i, ++k) {
            p[i] ^= static_cast<char>(mask[k]);
        }
    }

    /// 从大端字节序里读 n（1/2/4/8）字节。帧头里的长度字段是网络序，
    /// 按主机序直接 memcpy 到整数在小端机上会读反。
    [[nodiscard]] static std::uint64_t read_be(const char *data,
                                               size_t n) noexcept {
        std::uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) {
            v = (v << 8) | static_cast<std::uint8_t>(data[i]);
        }
        return v;
    }

    /// 按大端写 n 字节。n == 2 或 8。
    static void append_be(std::string &out, std::uint64_t v, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            const size_t shift = 8 * (n - 1 - i);
            out.push_back(static_cast<char>((v >> shift) & 0xFF));
        }
    }

    bool fin{true};
    Opcode opcode{Opcode::kText};
    std::string payload;
};
} // namespace netx::websocket::details
