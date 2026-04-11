#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <unordered_map>
namespace netx
{
namespace http
{
namespace details
{
enum class ResponseType : std::uint8_t
{
	kBody,
	kFile
};
} // namespace details

struct Response
{
	Response& with_status(int code) noexcept
	{
		status_code = code;
		status_msg_ = code2msg(code);

		return *this;
	}

	Response& with_header(const std::string& key, const std::string& value)
	{
		header_params_[key] = value;

		return *this;
	}

	Response& content_type(const std::string& type)
	{
		with_header("Content-Type", type);

		return *this;
	}

	Response& keep_alive(bool on = true)
	{
		if (on)
		{
			with_header("Connection", "keep-alive");
		}
		else
		{
			with_header("Connection", "close");
		}

		return *this;
	}

	std::string to_formatted_string() const;

	Response& with_file(const std::string& path)
	{
		type = details::ResponseType::kFile;
		file = path;

		this->content_type(guess_mime_type(path));
		return *this;
	}

	Response& with_body(const std::string& entity)
	{
		type = details::ResponseType::kBody;
		body = entity;
		with_header("Content-Length", std::to_string(body.size()));

		return *this;
	}

	Response& with_body(std::string&& entity)
	{
		type = details::ResponseType::kBody;
		body = std::move(entity);
		with_header("Content-Length", std::to_string(body.size()));

		return *this;
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

  public:
	int status_code{404};
	std::string status_msg_;
	std::string version_{"HTTP/1.1"};
	std::unordered_map<std::string, std::string> header_params_;
	std::string body;

	details::ResponseType type{details::ResponseType::kBody};
	std::string file;
};

inline std::string Response::to_formatted_string() const
{
	std::string result;

	auto out = std::back_inserter(result);

	// 第一行
	std::format_to(out, "{} {} {}\r\n", version_, status_code, status_msg_);

	// 头部
	for (const auto& header : header_params_)
	{
		std::format_to(out, "{}: {}\r\n", header.first, header.second);
	}

	// 空行和正文
	std::format_to(out, "\r\n{}", body);

	return result;
}
} // namespace http
} // namespace netx