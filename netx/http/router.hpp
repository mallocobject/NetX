#ifndef NETX_HTTP_ROUTER_HPP
#define NETX_HTTP_ROUTER_HPP

#include "netx/async/task.hpp"
#include "netx/http/radix_tree.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include <cassert>
#include <fcntl.h>
#include <functional>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>
namespace netx
{
namespace http
{
namespace async = netx::async;

using HttpHandler = std::function<async::Task<HttpResponse>(HttpRequest*)>;

class HttpRouter
{
  public:
	template <typename Handler>
	void route(const std::string& method, const std::string& path,
			   Handler&& handler)
	{
		trees_[method].insert(path, std::forward<Handler>(handler));
	}

	async::Task<HttpResponse> dispatch(HttpRequest* req);

	HttpRouter() = default;
	HttpRouter(HttpRouter&&) = delete;
	~HttpRouter() = default;

  private:
	static std::string get_mine_type(const std::string& path);

  private:
	std::unordered_map<std::string, RadixTree<HttpHandler>> trees_;
};

inline async::Task<HttpResponse> HttpRouter::dispatch(HttpRequest* req)
{
	std::string clean_path = RadixTree<HttpHandler>::normalize_path(req->path);
	req->path = clean_path;

	auto match = trees_[req->method].search(clean_path);

	HttpHandler handler;

	if (match.value) // match.value 是指向 HttpHandler 的指针
	{
		req->path_params = std::move(match.params);

		co_return co_await (*match.value)(req);
	}
	co_return std::move(
		HttpResponse{}.status(404).body("<h1>404 Not Found</h1>"));
}

} // namespace http
} // namespace netx

#endif