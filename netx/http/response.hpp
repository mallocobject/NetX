#pragma once

#include "netx/http/field_map.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace netx::http {
namespace details {
enum class ResponseType : std::uint8_t { kBody, kFile };
} // namespace details

class Response {
  public:
    Response &with_status(int code) noexcept {
        status_code = code;
        status_msg_ = code2msg(code);
        return *this;
    }

    Response &with_header(std::string_view key, std::string_view value) {
        header_params_[key] = value;
        return *this;
    }

    Response &content_type(std::string_view type) {
        return with_header("Content-Type", type);
    }

    Response &keep_alive(bool on = true) {
        return with_header("Connection", on ? "keep-alive" : "close");
    }

    [[nodiscard]] std::string to_head_string() const;
    [[nodiscard]] std::string to_formatted_string() const;

    Response &with_file(const std::string &path) {
        type = details::ResponseType::kFile;
        file = path;
        return content_type(guess_mime_type(path));
    }

    /// 字符串字面量要单独接一下：它在 string_view 和 string&& 之间是
    /// 二义转换，两边都是用户定义转换。
    Response &with_body(const char *entity) {
        return with_body(std::string_view{entity});
    }

    Response &with_body(std::string_view entity) {
        type = details::ResponseType::kBody;
        body.assign(entity);
        return with_header("Content-Length", std::to_string(body.size()));
    }

    Response &with_body(std::string &&entity) {
        type = details::ResponseType::kBody;
        body = std::move(entity);
        return with_header("Content-Length", std::to_string(body.size()));
    }

  private:
    [[nodiscard]] static std::string_view code2msg(int code) noexcept {
        switch (code) {
        case 101:
            return "Switching Protocols";
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 304:
            return "Not Modified";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 408:
            return "Request Timeout";
        case 500:
            return "Internal Server Error";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        default:
            return "Unknown";
        }
    }

    [[nodiscard]] static std::string_view
    guess_mime_type(const std::string &path) noexcept {
        // 只看最后一个 '.' 之后的部分，带点的目录名不会误判
        const size_t dot = path.rfind('.');
        if (dot == std::string::npos) {
            return "application/octet-stream";
        }
        const std::string_view ext = std::string_view{path}.substr(dot);

        if (ext == ".html" || ext == ".htm") {
            return "text/html; charset=utf-8";
        } else if (ext == ".css") {
            return "text/css; charset=utf-8";
        } else if (ext == ".js" || ext == ".mjs") {
            return "application/javascript; charset=utf-8";
        } else if (ext == ".json") {
            return "application/json; charset=utf-8";
        } else if (ext == ".svg") {
            return "image/svg+xml; charset=utf-8";
        } else if (ext == ".png") {
            return "image/png";
        } else if (ext == ".jpg" || ext == ".jpeg") {
            return "image/jpeg";
        } else if (ext == ".gif") {
            return "image/gif";
        } else if (ext == ".webp") {
            return "image/webp";
        } else if (ext == ".ico") {
            return "image/x-icon";
        } else if (ext == ".txt") {
            return "text/plain; charset=utf-8";
        } else if (ext == ".mp4") {
            return "video/mp4";
        }
        return "application/octet-stream";
    }

  public:
    int status_code{404};
    std::string status_msg_{code2msg(404)};
    std::string version_{"HTTP/1.1"};
    details::FieldMap header_params_;
    std::string body;

    details::ResponseType type{details::ResponseType::kBody};
    std::string file;
};

inline std::string Response::to_head_string() const {
    std::string result;
    result.reserve(64 + (header_params_.size() * 24));

    result += version_;
    result.push_back(' ');
    // 状态码是三位数，直接拼比 to_string / format 都省一次分配
    const char code[4] = {static_cast<char>('0' + (status_code / 100) % 10),
                          static_cast<char>('0' + (status_code / 10) % 10),
                          static_cast<char>('0' + status_code % 10),
                          '\0'};
    result += code;
    result.push_back(' ');
    result += status_msg_;
    result += "\r\n";

    for (const auto &[key, value] : header_params_) {
        result += key;
        result += ": ";
        result += value;
        result += "\r\n";
    }
    result += "\r\n";

    return result;
}

inline std::string Response::to_formatted_string() const {
    std::string result = to_head_string();
    result += body;
    return result;
}
} // namespace netx::http
