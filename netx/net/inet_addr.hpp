#ifndef NETX_NET_INET_ADDR_HPP
#define NETX_NET_INET_ADDR_HPP

#include "netx/async/check_error.hpp"
#include <arpa/inet.h>
#include <cstdint>
#include <format>
#include <netinet/in.h>
#include <string>
#include <strings.h>
namespace netx
{
namespace net
{
namespace async = netx::async;
class InetAddr
{
  public:
	InetAddr()
	{
		bzero(&addr_, sizeof(addr_));
	}

	explicit InetAddr(std::uint16_t port, bool loopback_only = false)
	{
		bzero(&addr_, sizeof(addr_));
		addr_.sin_family = AF_INET;
		addr_.sin_port = htons(port);
		in_addr_t ip = loopback_only ? INADDR_LOOPBACK : INADDR_ANY;
		addr_.sin_addr.s_addr = htonl(ip);
	}

	InetAddr(const std::string& ip, std::uint16_t port)
	{
		bzero(&addr_, sizeof(addr_));
		addr_.sin_family = AF_INET;
		addr_.sin_port = htons(port);
		async::checkError(
			inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr.s_addr));
	}

	explicit InetAddr(const sockaddr_in& addr) : addr_(addr)
	{
	}

	const struct sockaddr* sockaddr() const
	{
		return reinterpret_cast<const struct sockaddr*>(&addr_);
	}

	struct sockaddr* sockaddr()
	{
		return reinterpret_cast<struct sockaddr*>(&addr_);
	}

	void set_sockaddr(const sockaddr_in& addr)
	{
		addr_ = addr;
	}

	std::string ip() const
	{
		char buf[INET_ADDRSTRLEN];
		bzero(buf, sizeof(buf));
		if (inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf)) == nullptr)
		{
			async::checkError(-1);
		}

		return std::string(buf);
	}

	std::uint16_t port() const
	{
		return ::ntohs(addr_.sin_port);
	}

	std::string to_formatted_string() const
	{
		return std::format("{}:{}", ip(), port());
	}

  private:
	sockaddr_in addr_;
};
} // namespace net
} // namespace netx

#endif