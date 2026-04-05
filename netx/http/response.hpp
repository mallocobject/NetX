#ifndef NETX_HTTP_RESPONSE_HPP
#define NETX_HTTP_RESPONSE_HPP

#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>
namespace netx
{
namespace http
{
enum class ResponseType : std::uint8_t
{
	kBody,
	kFile
};

struct HttpResponse
{
	HttpResponse& status(int code) noexcept
	{
		status_code_ = code;
		status_msg_ = code2msg(code);

		return *this;
	}

	int status_code() const noexcept
	{
		return status_code_;
	}

	HttpResponse& header(const std::string& key, const std::string& value)
	{
		header_params_[key] = value;

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

	HttpResponse& file(const std::string& path)
	{
		type_ = ResponseType::kFile;
		file_ = path;

		this->content_type(guess_mime_type(path));
		return *this;
	}

	HttpResponse& body(const std::string& body)
	{
		type_ = ResponseType::kBody;
		body_ = body;
		header("Content-Length", std::to_string(body_.size()));

		return *this;
	}

	HttpResponse& body(std::string&& body)
	{
		type_ = ResponseType::kBody;
		body_ = std::move(body);
		header("Content-Length", std::to_string(body_.size()));

		return *this;
	}

	ResponseType type() const
	{
		return type_;
	}

	const std::string& get_file() const
	{
		return file_;
	}

	const std::string& get_body() const
	{
		return body_;
	}

  private:
	static std::string_view code2msg(int code)
	{
		static const std::unordered_map<int, std::string_view> status_msgs = {
			{200, "OK"},
			{201, "Created"},
			{204, "No Content"},
			{301, "Moved Permanently"},
			{302, "Found"},
			{304, "Not Modified"},
			{400, "Bad Request"},
			{401, "Unauthorized"},
			{403, "Forbidden"},
			{404, "Not Found"},
			{405, "Method Not Allowed"},
			{408, "Request Timeout"},
			{500, "Internal Server Error"},
			{502, "Bad Gateway"},
			{503, "Service Unavailable"},
		};

		auto it = status_msgs.find(code);
		return (it != status_msgs.end()) ? it->second : "Unknown";
	}

	static std::string guess_mime_type(const std::string& path)
	{
		if (path.ends_with(".html"))
		{
			return "text/html; charset=utf-8";
		}
		else if (path.ends_with(".css"))
		{
			return "text/css; charset=utf-8";
		}
		else if (path.ends_with(".js"))
		{
			return "application/javascript; charset=utf-8";
		}
		else if (path.ends_with(".svg"))
		{
			return "image/svg+xml; charset=utf-8";
		}
		else if (path.ends_with(".png"))
		{
			return "image/png";
		}
		else if (path.ends_with(".jpg") || path.ends_with(".jpeg"))
		{
			return "image/jpeg";
		}

		return "application/octet-stream";
	}

  private:
	int status_code_{404};
	std::string status_msg_;
	std::string version_{"HTTP/1.1"};
	std::unordered_map<std::string, std::string> header_params_;
	std::string body_;

	ResponseType type_{ResponseType::kBody};
	std::string file_;
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