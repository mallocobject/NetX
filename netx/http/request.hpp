#pragma once

#include "netx/http/field_map.hpp"
#include <string>
#include <string_view>

namespace netx {
namespace http {

struct Request {
    /// header 名由解析器统一转小写后存入，这里按原文查即可。
    /// 返回 string_view 而不是 string —— 原来每次取值都白拷一份。
    [[nodiscard]] std::string_view header(std::string_view key) const {
        return header_params.get(key);
    }

    [[nodiscard]] std::string_view query(std::string_view key) const {
        return query_params.get(key);
    }

    [[nodiscard]] std::string_view path(std::string_view key) const {
        return path_params.get(key);
    }

    void clear() {
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
    Request(Request &&) = default;
    ~Request() = default;

    std::string method;
    std::string url_path;
    std::string version;

    FieldMap header_params;
    FieldMap query_params;
    FieldMap path_params;

    std::string body;
    bool keep_alive{false};
    size_t ctx_len{0};
};
} // namespace http
} // namespace netx
