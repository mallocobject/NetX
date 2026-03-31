#ifndef NETX_HTTP_RESPONSE_HPP
#define NETX_HTTP_RESPONSE_HPP

#include <format>
#include <string>
#include <unordered_map>
#include <utility>
namespace netx
{
namespace http
{
struct HttpResponse
{

	HttpResponse& status(int code) noexcept
	{
		status_code_ = code;
		status_msg_ = code2msg(code);

		return *this;
	}

	HttpResponse& header(const std::string& key, const std::string& value)
	{
		header_params_.try_emplace(key, value);

		return *this;
	}

	HttpResponse& body(const std::string& body)
	{
		body_ = body;
		header("Content-Length", std::to_string(body_.size()));

		return *this;
	}

	HttpResponse& body(std::string&& body)
	{
		body_ = std::move(body);
		header("Content-Length", std::to_string(body_.size()));

		return *this;
	}

	HttpResponse& content_type(const std::string& type)
	{
		header("Content-Type", type);

		return *this;
	}

	HttpResponse& keep_alive(bool on = true)
	{
		if (on)
		{
			header("Connection", "keep-alive");
		}
		else
		{
			header("Connection", "close");
		}

		return *this;
	}

	std::string to_formatted_string() const;

	HttpResponse() = default;
	HttpResponse(HttpResponse&&) = delete;
	~HttpResponse() = default;

  private:
	static std::string_view code2msg(int code)
	{
		static const std::unordered_map<int, std::string_view> status_msgs = {
			{200, "OK"},		  {301, "Moved Permanently"},
			{400, "Bad Request"}, {403, "Forbidden"},
			{404, "Not Found"},	  {500, "Internal Server Error"},
		};

		auto it = status_msgs.find(code);
		return (it != status_msgs.end()) ? it->second : "Unknown";
	}

  private:
	int status_code_{404};
	std::string status_msg_;
	std::string version_{"HTTP/1.1"};
	std::unordered_map<std::string, std::string> header_params_;
	std::string body_;
};

inline std::string HttpResponse::to_formatted_string() const
{
	std::string result;

	auto out = std::back_inserter(result);

	// 第一行
	std::format_to(out, "{} {} {}\r\n", version_, status_code_, status_msg_);

	// 头部
	for (const auto& header : header_params_)
	{
		std::format_to(out, "{}: {}\r\n", header.first, header.second);
	}

	// 空行和正文
	std::format_to(out, "\r\n{}", body_);

	return result;
}
} // namespace http
} // namespace netx

#endif