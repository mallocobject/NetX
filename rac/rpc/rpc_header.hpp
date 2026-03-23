#ifndef RAC_RPC_RPC_HEADER_HPP
#define RAC_RPC_RPC_HEADER_HPP

#include "rac/meta/buffer_endian_helper.hpp"
#include <cstdint>
#include <netinet/in.h>
#include <ostream>
namespace rac
{
static constexpr std::uint32_t kMagic =
	0x31434152; // "RAC1" (little-endian memory)
static constexpr std::uint8_t kVersion = 1;

#pragma pack(push, 1)
// net
struct RpcHeaderWire
{
	std::uint32_t magic;
	std::uint8_t version;
	std::uint8_t flags;
	std::uint16_t header_len;
	std::uint32_t body_len; // Payload length
	std::uint64_t request_id;
	std::uint32_t reserved;
};
#pragma pack(pop)

// host
struct RpcHeader
{
	std::uint32_t magic{};
	std::uint8_t version{};
	std::uint8_t flags{};
	std::uint16_t header_len{};
	std::uint32_t body_len{};
	std::uint64_t request_id{};
	std::uint32_t reserved{};
};

static constexpr std::uint16_t kRpcHeaderWireLength = sizeof(RpcHeaderWire);
static constexpr std::uint16_t kRpcHeaderLength = sizeof(RpcHeader);

inline RpcHeader to_host(const RpcHeaderWire& w)
{
	RpcHeader h;
	h.magic = beToHost(w.magic);
	h.version = beToHost(w.version);
	h.flags = beToHost(w.flags);
	h.header_len = beToHost(w.header_len);
	h.body_len = beToHost(w.body_len);
	h.request_id = beToHost(w.request_id);
	h.reserved = beToHost(w.reserved);
	return h;
}

inline RpcHeaderWire to_wire(const RpcHeader& h)
{
	RpcHeaderWire w;
	w.magic = hostToBE(h.magic);
	w.version = hostToBE(h.version);
	w.flags = hostToBE(h.flags);
	w.header_len = hostToBE(h.header_len);
	w.body_len = hostToBE(h.body_len);
	w.request_id = hostToBE(h.request_id);
	w.reserved = hostToBE(h.reserved);
	return w;
}

inline std::ostream& operator<<(std::ostream& os, const RpcHeader& h)
{
	os << "RpcHeader{\n"
	   << "  magic: 0x" << std::hex << h.magic << std::dec << "\n"
	   << "  version: " << static_cast<unsigned>(h.version) << "\n"
	   << "  flags: 0x" << std::hex << static_cast<unsigned>(h.flags)
	   << std::dec << "\n"
	   << "  header_len: " << h.header_len << "\n"
	   << "  body_len: " << h.body_len << "\n"
	   << "  request_id: " << h.request_id << "\n"
	   << "}";
	return os;
}
} // namespace rac

#endif