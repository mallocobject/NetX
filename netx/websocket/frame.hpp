#pragma once

#include <cstdint>
#include <string>
namespace netx
{
namespace websocket
{
namespace details
{
enum class Opcode : std::uint8_t
{
	kContinuation = 0x0,
	kText = 0x1,
	kBinary = 0x2,
	kClose = 0x8,
	kPing = 0x9,
	kPong = 0xA
};

struct Frame
{
	bool fin{true};
	Opcode opcode{Opcode::kText};
	std::string payload;

	static void apply_mask(std::string* data, const uint8_t mask[4])
	{
		for (size_t i = 0; i < data->size(); ++i)
		{
			data->at(i) ^= mask[i % 4];
		}
	}
};
} // namespace details
} // namespace websocket
} // namespace netx