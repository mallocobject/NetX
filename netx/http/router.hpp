#ifndef NETX_HTTP_ROUTER_HPP
#define NETX_HTTP_ROUTER_HPP

#include "netx/async/task.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/net/stream.hpp"
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
namespace netx
{
namespace http
{
namespace async = netx::async;
namespace net = netx::net;
using TaskType = async::Task<>;
using HttpHandler =
	std::function<TaskType(const HttpRequest&, HttpResponse*, net::Stream*)>;
class HttpRouter
{
	struct RadixTree
	{
		struct RadixNode
		{
			HttpHandler f{nullptr};
			std::unordered_map<std::string, std::unique_ptr<RadixNode>> next;
		};

		void insert(const std::string& path, HttpHandler handler);
		HttpHandler search(const std::string& path);

		RadixTree() : root(std::make_unique<RadixNode>())
		{
		}

		RadixTree(RadixTree&&) = delete;
		~RadixTree() = default;

	  private:
		std::string subPath(const std::string& path, std::size_t* start);

	  private:
		std::unique_ptr<RadixNode> root;
	};

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
	std::unordered_map<std::string, RadixTree> trees_;
};

inline void HttpRouter::RadixTree::insert(const std::string& path,
										  HttpHandler handler)
{
	if (path == "/")
	{
		root->f = std::move(handler);
		return;
	}

	std::size_t start = (path[0] == '/') ? 1 : 0;
	RadixNode* cur = root.get();

	while (start != std::string::npos)
	{
		std::string s = subPath(path, &start);
		if (s.empty() && start == std::string::npos)
		{
			break; // error
		}

		if (cur->next.find(s) == cur->next.end())
		{
			cur->next[s] = std::make_unique<RadixNode>();
		}

		cur = cur->next[s].get();
	}

	cur->f = std::move(handler);
}

inline HttpHandler HttpRouter::RadixTree::search(const std::string& path)
{
	if (path == "/")
	{
		return root->f;
	}

	size_t start = (path[0] == '/') ? 1 : 0;
	RadixNode* cur = root.get();

	while (start != std::string::npos)
	{
		std::string s = subPath(path, &start);
		if (s.empty() && start == std::string::npos)
		{
			break; // error
		}

		if (cur->next.find(s) == cur->next.end())
		{
			return nullptr; // router not exist
		}

		cur = cur->next[s].get();
	}

	return cur->f;
}

inline std::string HttpRouter::RadixTree::subPath(const std::string& path,
												  std::size_t* start)
{
	// because *start maybe arive 1
	if (*start == std::string::npos || *start >= path.size())
	{
		*start = std::string::npos;
		return "";
	}

	size_t begin = *start;
	size_t end = path.find('/', begin);

	if (end != std::string::npos)
	{
		*start = end + 1;
		return path.substr(begin, end - begin);
	}

	*start = std::string::npos;
	return path.substr(begin);
}

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

		co_return;
	}
}
} // namespace http
} // namespace netx

#endif