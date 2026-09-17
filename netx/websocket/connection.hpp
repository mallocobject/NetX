#pragma once

#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/net/stream.hpp"
#include "netx/websocket/frame.hpp"
#include <array>
#include <cstring>
#include <string_view>

namespace netx::websocket::details {
/// 一条 WebSocket 连接。只做帧的收发，不做分片重组 —— 调用方拿到
/// kContinuation 帧后自行拼接。
class Connection {
  public:
    inline static constexpr size_t kMaxPayloadSize = 10 * 1024 * 1024;

    core::Task<core::Expected<Frame>> receive();

    core::Task<core::Expected<>> send_continuation(std::string_view data) {
        co_return co_await send_frame(Opcode::kContinuation, data);
    }
    core::Task<core::Expected<>> send_text(std::string_view data) {
        co_return co_await send_frame(Opcode::kText, data);
    }
    core::Task<core::Expected<>> send_binary(std::string_view data) {
        co_return co_await send_frame(Opcode::kBinary, data);
    }
    core::Task<core::Expected<>> send_close(std::string_view data) {
        co_return co_await send_frame(Opcode::kClose, data);
    }
    core::Task<core::Expected<>> send_ping(std::string_view data) {
        co_return co_await send_frame(Opcode::kPing, data);
    }
    core::Task<core::Expected<>> send_pong(std::string_view data) {
        co_return co_await send_frame(Opcode::kPong, data);
    }

    explicit Connection(net::details::Stream &stream) : stream_(stream) {
    }

  private:
    core::Task<core::Expected<>>
    send_frame(Opcode opcode, std::string_view payload, bool fin = true);

    /// 攒够 n 字节再返回。只在需要时才真的读，所以一个帧最多读两次。
    core::Task<core::Expected<>> ensure_read(size_t n) {
        while (stream_.read_buf.readable_bytes() < n) {
            co_await co_await stream_.read();
        }
        co_return {};
    }

    net::details::Stream &stream_;
};

inline core::Task<core::Expected<Frame>> Connection::receive() {
    auto &buf = stream_.read_buf;

    // 先取 2 字节固定头：长度字段的宽度和有没有掩码都由它决定
    co_await co_await ensure_read(2);
    const std::uint8_t b1 = static_cast<std::uint8_t>(buf.peek()[0]);
    const std::uint8_t b2 = static_cast<std::uint8_t>(buf.peek()[1]);
    buf.retrieve(2);

    const bool fin = (b1 & 0x80) != 0;
    const auto raw_opcode = static_cast<std::uint8_t>(b1 & 0x0F);

    // RSV1-3 必须为 0（没协商任何扩展），opcode 也不能是保留值
    if ((b1 & 0x70) != 0 || !is_known_opcode(raw_opcode)) {
        co_return core::details::make_error_to_unexpected(
            core::details::Error::InvalidOperation);
    }
    const auto opcode = static_cast<Opcode>(raw_opcode);

    const bool has_mask = (b2 & 0x80) != 0;
    const std::uint8_t len7 = b2 & 0x7F;

    // 扩展长度字段：126 -> 2 字节，127 -> 8 字节，都是网络序
    const size_t ext = (len7 == 126) ? 2 : (len7 == 127) ? 8 : 0;
    std::uint64_t payload_len = len7;
    if (ext > 0) {
        co_await co_await ensure_read(ext);
        payload_len = Frame::read_be(buf.peek(), ext);
        buf.retrieve(ext);
    }

    if (payload_len > kMaxPayloadSize) {
        co_return core::details::make_error_to_unexpected(
            core::details::Error::InvalidOperation);
    }

    // 控制帧不能分片，载荷也不能超过 125 字节
    if (is_control(opcode) && (!fin || payload_len > 125)) {
        co_return core::details::make_error_to_unexpected(
            core::details::Error::InvalidOperation);
    }

    const size_t mask_len = has_mask ? 4 : 0;

    // 掩码和载荷一次读齐：分几次读就是一个帧要读好几回
    if (mask_len + payload_len > 0) {
        co_await co_await ensure_read(mask_len + payload_len);
    }

    Frame frame{.fin = fin, .opcode = opcode, .payload = {}};

    if (has_mask) {
        std::uint8_t mask[4] = {};
        std::memcpy(mask, buf.peek(), 4);
        buf.retrieve(4);

        if (payload_len > 0) {
            frame.payload = buf.retrieve_string(payload_len);
            Frame::apply_mask(&frame.payload, mask);
        }
    } else if (payload_len > 0) {
        frame.payload = buf.retrieve_string(payload_len);
    }

    co_return frame;
}

inline core::Task<core::Expected<>>
Connection::send_frame(Opcode opcode, std::string_view payload, bool fin) {
    if (is_control(opcode) && payload.size() > 125) {
        co_return core::details::make_error_to_unexpected(
            core::details::Error::InvalidOperation);
    }

    std::string header;
    header.reserve(10);
    header.push_back(static_cast<char>((fin ? 0x80 : 0x00) |
                                       static_cast<std::uint8_t>(opcode)));

    if (payload.size() <= 125) {
        header.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 0xFFFF) {
        header.push_back(126);
        Frame::append_be(header, payload.size(), 2);
    } else {
        header.push_back(127);
        Frame::append_be(header, payload.size(), 8);
    }

    // 头与载荷一次 writev 发出去：分成两次 write 就是两次系统调用
    const std::array<std::string_view, 2> parts{header, payload};
    co_return co_await stream_.write_many(parts);
}
} // namespace netx::websocket::details
