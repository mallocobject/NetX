#pragma once

#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/http/radix_tree.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/websocket/connection.hpp"
namespace netx
{
namespace http
{
namespace details
{
using HttpHandler =
	std::function<core::Task<core::Expected<Response>>(Request&)>;
using WsHandler = std::function<core::Task<core::Expected<>>(
	websocket::details::Connection&)>;

struct Router
{
	template <typename Handler>
	void route(const std::string& method, const std::string& path,
			   Handler&& handler)
	{
		trees_[method].insert(path, std::forward<Handler>(handler));
	}

	template <typename Handler>
	void route_ws(const std::string& path, Handler&& handler)
	{
		route("GET", path,
			  [](Request& req) -> core::Task<core::Expected<Response>>
			  {
				  Response res;
				  if (req.header("upgrade") == "websocket")
				  {
					  res.with_status(101);
				  }
				  co_return res;
			  });

		ws_tree_.insert(path, std::move(handler));
	}

	WsHandler get_ws_handler(const std::string& path)
	{
		auto match = ws_tree_.search(path);
		return match.value ? *match.value : nullptr;
	}

	core::Task<core::Expected<Response>> dispatch(Request& req);

	Router() = default;
	Router(Router&&) = default;
	~Router() = default;

  private:
	static std::string get_mine_type(const std::string& path);

  private:
	std::unordered_map<std::string, RadixTree<HttpHandler>> trees_;
	RadixTree<WsHandler> ws_tree_;
};

inline core::Task<core::Expected<Response>> Router::dispatch(Request& req)
{
	std::string clean_path =
		RadixTree<HttpHandler>::normalize_path(req.url_path);
	req.url_path = clean_path;

	auto match = trees_[req.method].search(clean_path);

	HttpHandler handler;

	if (match.value) // match.value 是指向 HttpHandler 的指针
	{
		req.path_params = std::move(match.params);

		co_return co_await (*match.value)(req);
	}
	co_return Response{}.with_status(404).with_body("<h1>404 Not Found</h1>");
}

} // namespace details
} // namespace http
} // namespace netx