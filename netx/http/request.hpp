#ifndef NETX_HTTP_REQUEST_HPP
#define NETX_HTTP_REQUEST_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
namespace netx
{
namespace http
{
struct HttpRequest
{
	std::string method;
	std::string path;
	std::string version;

	std::unordered_map<std::string, std::string> header_params;
	std::unordered_map<std::string, std::string> query_params;
	std::unordered_map<std::string, std::string> path_params;

	std::string body;

	bool keep_alive{false};

	std::size_t ctx_len{0};

	std::string header(const std::string& key) const
	{
		auto it = header_params.find(key);
		return (it != header_params.end()) ? it->second : "";
	}

	std::string query(const std::string& key) const
	{
		auto it = query_params.find(key);
		return (it != query_params.end()) ? it->second : "";
	}

	std::string param(const std::string& key) const
	{
		auto it = path_params.find(key);
		return (it != path_params.end()) ? it->second : "";
	}

	void clear()
	{
		method.clear();
		path.clear();
		version.clear();
		header_params.clear();
		query_params.clear();
		body.clear();
		keep_alive = false;
		ctx_len = 0;
	}

	HttpRequest() = default;
	HttpRequest(HttpRequest&&) = delete;
	~HttpRequest() = default;
};
} // namespace http
} // namespace netx

#endif