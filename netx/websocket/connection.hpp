#pragma once

#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/net/stream.hpp"
#include "netx/websocket/frame.hpp"
namespace netx
{
namespace websocket
{
namespace details
{
struct Connection
{
	inline static constexpr size_t kMaxPayloadSize = 10 * 1024 * 1024; // 10MB

	core::Task<core::Expected<Frame>> receive();

	core::Task<core::Expected<>> send_continuation(const std::string& data)
	{
		co_return co_await send_frame(Opcode::kContinuation, data);
	}
	core::Task<core::Expected<>> send_text(const std::string& data)
	{
		co_return co_await send_frame(Opcode::kText, data);
	}
	core::Task<core::Expected<>> send_binary(const std::string& data)
	{
		co_return co_await send_frame(Opcode::kBinary, data);
	}
	core::Task<core::Expected<>> send_close(const std::string& data)
	{
		co_return co_await send_frame(Opcode::kClose, data);
	}
	core::Task<core::Expected<>> send_ping(const std::string& data)
	{
		co_return co_await send_frame(Opcode::kPing, data);
	}
	core::Task<core::Expected<>> send_pong(const std::string& data)
	{
		co_return co_await send_frame(Opcode::kPong, data);
	}

	explicit Connection(net::details::Stream& stream) : stream_(stream)
	{
	}

  private:
	core::Task<core::Expected<>> send_frame(Opcode opcode,
											const std::string& payload,
											bool fin = true);

	core::Task<core::Expected<>> ensure_read(size_t n)
	{
		while (stream_.read_buf.readable_bytes() < n)
		{
			co_await co_await stream_.read();
		}
		co_return {};
	}

  private:
	net::details::Stream& stream_;
};

inline core::Task<core::Expected<Frame>> Connection::receive()
{
	auto& buf = stream_.read_buf;

	co_await co_await ensure_read(2);
	std::uint8_t b1 = buf.retrieve_integer<std::uint8_t>();
	std::uint8_t b2 = buf.retrieve_integer<std::uint8_t>();

	Frame frame{.fin = (b1 & 0x80) != 0,
				.opcode = static_cast<Opcode>(b1 & 0x0f)};

	bool has_mask = (b2 & 0x80) != 0;
	std::uint64_t payload_len = b2 & 0x7f;

	if (payload_len == 126)
	{
		co_await co_await ensure_read(2);
		payload_len = buf.retrieve_integer<std::uint16_t>();
	}
	else if (payload_len == 127)
	{
		co_await co_await ensure_read(8);
		payload_len = buf.retrieve_integer<std::uint64_t>();
	}

	if (payload_len > kMaxPayloadSize)
	{
		co_return core::details::make_error_code(
			core::details::Error::InvalidOperation);
	}

	std::uint8_t mask[4];
	if (has_mask)
	{
		co_await co_await ensure_read(4);
		std::memcpy(mask, buf.peek(), 4);
		buf.retrieve(4);
	}

	if (payload_len > 0)
	{
		co_await co_await ensure_read(payload_len);
		frame.payload = buf.retrieve_string(payload_len);

		if (has_mask)
		{
			Frame::apply_mask(&frame.payload, mask);
		}
	}

	co_return frame;
}

inline core::Task<core::Expected<>> Connection::send_frame(
	Opcode opcode, const std::string& payload, bool fin)
{
	std::string header;
	header.push_back(static_cast<char>((fin ? 0x80 : 0x00) |
									   static_cast<std::uint8_t>(opcode)));

	if (payload.size() <= 125)
	{
		header.push_back(static_cast<char>(payload.size()));
	}
	else if (payload.size() <= 65535)
	{
		header.push_back(126);
		std::uint16_t len = htobe16(static_cast<std::uint16_t>(payload.size()));
		header.append(reinterpret_cast<const char*>(&len), 2);
	}
	else
	{
		header.push_back(127);
		std::uint16_t len = htobe64(payload.size());
		header.append(reinterpret_cast<const char*>(&len), 8);
	}

	co_await co_await stream_.write(header);
	if (!payload.empty())
	{
		co_await co_await stream_.write(payload);
	}

	co_return {};
}
} // namespace details
} // namespace websocket
} // namespace netx