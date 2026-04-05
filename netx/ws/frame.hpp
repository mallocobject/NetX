#ifndef NETX_WS_FRAME_HPP
#define NETX_WS_FRAME_HPP

#include <cstdint>
#include <string>
namespace netx
{
namespace ws
{
enum class WSOpcode : std::uint8_t
{
	kContinuation = 0x0,
	kText = 0x1,
	kBinary = 0x2,
	kClose = 0x8,
	kPing = 0x9,
	kPong = 0xA
};

struct WSFrame
{
	bool fin{true};
	WSOpcode opcode{WSOpcode::kText};
	std::string payload;

	static void apply_mask(std::string* data, const uint8_t mask[4])
	{
		for (size_t i = 0; i < data->size(); ++i)
		{
			data->at(i) ^= mask[i % 4];
		}
	}
};
} // namespace ws
} // namespace netx

#endif