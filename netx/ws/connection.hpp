#ifndef NETX_WS_CONNECTION_HPP
#define NETX_WS_CONNECTION_HPP

#include "netx/async/task.hpp"
#include "netx/net/stream.hpp"
#include "netx/ws/frame.hpp"
#include <cstdint>
#include <cstring>
#include <endian.h>
#include <optional>
#include <string>
namespace netx
{
namespace ws
{
namespace async = netx::async;
namespace net = netx::net;
class WSConnection
{
  public:
	async::Task<std::optional<WSFrame>> receive();

	async::Task<bool> send_continuation(const std::string& data)
	{
		co_return co_await send_frame(WSOpcode::kContinuation, data);
	}
	async::Task<bool> send_text(const std::string& data)
	{
		co_return co_await send_frame(WSOpcode::kText, data);
	}
	async::Task<bool> send_binary(const std::string& data)
	{
		co_return co_await send_frame(WSOpcode::kBinary, data);
	}
	async::Task<bool> send_close(const std::string& data)
	{
		co_return co_await send_frame(WSOpcode::kClose, data);
	}
	async::Task<bool> send_ping(const std::string& data)
	{
		co_return co_await send_frame(WSOpcode::kPing, data);
	}
	async::Task<bool> send_pong(const std::string& data)
	{
		co_return co_await send_frame(WSOpcode::kPong, data);
	}

	explicit WSConnection(net::Stream* stream) : stream_(stream)
	{
	}

  private:
	async::Task<bool> send_frame(WSOpcode opcode, const std::string& payload);

  private:
	net::Stream* stream_{nullptr};
};

inline async::Task<std::optional<WSFrame>> WSConnection::receive()
{
	if (stream_->read_buffer()->readableBytes() < 2)
	{
		if (!co_await stream_->read())
		{
			co_return std::nullopt;
		}
	}

	auto* buf = stream_->read_buffer();
	std::uint8_t b1 = buf->retrieveInt<std::uint8_t>();
	std::uint8_t b2 = buf->retrieveInt<std::uint8_t>();

	WSFrame frame{.fin = (b1 & 0x80) != 0,
				  .opcode = static_cast<WSOpcode>(b1 & 0x0f)};

	bool has_mask = (b2 & 0x80) != 0;
	std::uint64_t payload_len = b2 & 0x7f;

	if (payload_len == 126)
	{
		while (buf->readableBytes() < 2)
		{
			if (!co_await stream_->read())
			{
				co_return std::nullopt;
			}
		}
		payload_len = buf->retrieveInt<std::uint16_t>();
	}
	else if (payload_len == 127)
	{
		while (buf->readableBytes() < 8)
		{
			if (!co_await stream_->read())
			{
				co_return std::nullopt;
			}
		}
		payload_len = buf->retrieveInt<std::uint64_t>();
	}

	std::uint8_t mask[4];
	if (has_mask)
	{
		while (buf->readableBytes() < 4)
		{
			if (!co_await stream_->read())
			{
				co_return std::nullopt;
			}
		}
		std::memcpy(mask, buf->peek(), 4);
		buf->retrieve(4);
	}

	while (buf->readableBytes() < payload_len)
	{
		if (!co_await stream_->read())
		{
			co_return std::nullopt;
		}
	}
	frame.payload = buf->retrieve_string(payload_len);

	if (has_mask)
	{
		WSFrame::apply_mask(&frame.payload, mask);
	}

	co_return frame;
}

inline async::Task<bool> WSConnection::send_frame(WSOpcode opcode,
												  const std::string& payload)
{
	std::string header{
		static_cast<char>(0x80 | static_cast<std::uint8_t>(opcode))};

	if (payload.size() <= 125)
	{
		header += static_cast<char>(payload.size());
	}
	else if (payload.size() <= 65535)
	{
		header += 126;
		std::uint16_t len = htobe16(static_cast<std::uint16_t>(payload.size()));
		header.append(reinterpret_cast<const char*>(&len), 2);
	}
	else
	{
		header += 127;
		std::uint16_t len = htobe64(payload.size());
		header.append(reinterpret_cast<const char*>(&len), 8);
	}

	if (!co_await stream_->write(header))
	{
		co_return false;
	}
	if (!co_await stream_->write(payload))
	{
		co_return false;
	}
	co_return true;
}
} // namespace ws
} // namespace netx

#endif