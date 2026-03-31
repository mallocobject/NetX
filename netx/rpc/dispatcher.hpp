#ifndef NETX_RPC_DISPATCHER_HPP
#define NETX_RPC_DISPATCHER_HPP

#include "netx/meta/function_traits.hpp"
#include "netx/net/buffer.hpp"
#include "netx/rpc/serialize_traits.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
namespace netx
{
namespace rpc
{
namespace net = netx::net;
namespace meta = netx::meta;
class RpcDispatcher
{
  public:
	using RpcHandler = std::function<void(
		net::Buffer* req_buf, net::Buffer* res_buf, std::uint32_t limit)>;

	template <typename Func>
	void bind(const std::string& method_name, Func&& func);

	void dispatch(const std::string& method_name, net::Buffer* req_buf,
				  net::Buffer* res_buf, std::uint32_t limit)
	{
		if (auto it = handlers_.find(method_name); it != handlers_.end())
		{
			it->second(req_buf, res_buf, limit);
		}
		else
		{
			throw std::runtime_error("RPC Method not found: " + method_name);
		}
	}

  private:
	std::unordered_map<std::string, RpcHandler> handlers_;
};

template <typename Func>
void RpcDispatcher::bind(const std::string& method_name, Func&& func)
{
	using FunctionTraits =
		meta::FunctionTraits<std::decay_t<Func>>; // void(int) -> void(*)(int)
												  // no void(&)(int)
	using ArgsTuple = typename FunctionTraits::ArgsTuple;
	using RetType = typename FunctionTraits::RetType;

	handlers_.try_emplace(
		method_name,
		[func = std::forward<Func>(func)](net::Buffer* req, net::Buffer* res,
										  std::uint32_t limit)
		{
			ArgsTuple args;
			DeserializeTraits<ArgsTuple>::deserialize(req, &args, limit);

			if constexpr (std::is_void_v<RetType>)
			{
				std::apply(func, args);
			}
			else
			{
				RetType result = std::apply(func, args);
				using RetTuple = std::tuple<RetType>;
				RetTuple ret{result};
				SerializeTraits<RetTuple>::serialize(res, ret);
			}
		});
}
} // namespace rpc
} // namespace netx

#endif