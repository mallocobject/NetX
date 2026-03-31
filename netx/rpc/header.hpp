#ifndef NETX_RPC_RPC_HEADER_HPP
#define NETX_RPC_RPC_HEADER_HPP

#include "netx/meta/endian_helper.hpp"
#include <cstdint>
#include <netinet/in.h>
#include <ostream>
namespace netx
{
namespace rpc
{
namespace meta = netx::meta;

static constexpr std::uint32_t kMagic =
	0x5854454E; // "NETX" (little-endian memory)
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
	h.magic = meta::beToHost(w.magic);
	h.version = meta::beToHost(w.version);
	h.flags = meta::beToHost(w.flags);
	h.header_len = meta::beToHost(w.header_len);
	h.body_len = meta::beToHost(w.body_len);
	h.request_id = meta::beToHost(w.request_id);
	h.reserved = meta::beToHost(w.reserved);
	return h;
}

inline RpcHeaderWire to_wire(const RpcHeader& h)
{
	RpcHeaderWire w;
	w.magic = meta::hostToBE(h.magic);
	w.version = meta::hostToBE(h.version);
	w.flags = meta::hostToBE(h.flags);
	w.header_len = meta::hostToBE(h.header_len);
	w.body_len = meta::hostToBE(h.body_len);
	w.request_id = meta::hostToBE(h.request_id);
	w.reserved = meta::hostToBE(h.reserved);
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
} // namespace rpc
} // namespace netx

#endif