#pragma once

#include <string>
#include <unordered_map>
namespace netx
{
namespace http
{
struct Request
{

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

	std::string path(const std::string& key) const
	{
		auto it = path_params.find(key);
		return (it != path_params.end()) ? it->second : "";
	}

	void clear()
	{
		method.clear();
		url_path.clear();
		version.clear();
		header_params.clear();
		query_params.clear();
		path_params.clear();
		body.clear();
		keep_alive = false;
		ctx_len = 0;
	}

	Request() = default;
	Request(Request&&) = default;
	~Request() = default;

	std::string method;
	std::string url_path;
	std::string version;

	std::unordered_map<std::string, std::string> header_params;
	std::unordered_map<std::string, std::string> query_params;
	std::unordered_map<std::string, std::string> path_params;

	std::string body;
	bool keep_alive{false};
	std::size_t ctx_len{0};
};
} // namespace http
} // namespace netx