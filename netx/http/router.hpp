#ifndef NETX_HTTP_ROUTER_HPP
#define NETX_HTTP_ROUTER_HPP

#include "netx/async/task.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/meta/radix_tree.hpp"
#include "netx/net/stream.hpp"
#include <cassert>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
namespace netx
{
namespace http
{
namespace async = netx::async;
namespace net = netx::net;
namespace meta = netx::meta;
using TaskType = async::Task<>;
using HttpHandler =
	std::function<TaskType(const HttpRequest&, HttpResponse*, net::Stream*)>;
class HttpRouter
{
  public:
	template <typename Handler>
	void route(const std::string& method, const std::string& path,
			   Handler&& handler)
	{
		trees_[method].insert(path, std::forward<Handler>(handler));
	}

	TaskType dispatch(const HttpRequest& req, HttpResponse* res,
					  net::Stream* stream);

	HttpRouter() = default;
	HttpRouter(HttpRouter&&) = delete;
	~HttpRouter() = default;

  private:
	std::unordered_map<std::string, meta::RadixTree<HttpHandler>> trees_;
};

inline TaskType HttpRouter::dispatch(const HttpRequest& req, HttpResponse* res,
									 net::Stream* stream)
{
	HttpHandler f;
	std::string method = req.method;
	std::string path = req.path;

	if ((f = trees_[method].search(path)))
	{
		co_await f(req, res, stream);
	}
	else
	{
		res->status(404)
			.content_type("text/html")
			.body("<h1>404 Not Found</h1>");

		co_await stream->write(res->to_formatted_string());
	}
}
} // namespace http
} // namespace netx

#endif