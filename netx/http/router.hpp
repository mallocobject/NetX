#pragma once

#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/http/radix_tree.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/websocket/connection.hpp"
#include <concepts>
#include <type_traits>

namespace netx::http::details {
using HttpHandler =
    std::function<core::Task<core::Expected<Response>>(Request &)>;
using WsHandler = std::function<core::Task<core::Expected<>>(
    websocket::details::Connection &)>;

struct Router {
    template <typename Handler>
        requires std::invocable<Handler, Request &> &&
                 std::same_as<std::invoke_result_t<Handler, Request &>,
                              core::Task<core::Expected<Response>>>
    [[nodiscard]] bool route(const std::string &method,
                             const std::string &path,
                             Handler &&handler) {
        return trees_[method].insert(path, std::forward<Handler>(handler));
    }

    template <typename Handler>
        requires std::invocable<Handler, websocket::details::Connection &> &&
                 std::same_as<
                     std::invoke_result_t<Handler,
                                          websocket::details::Connection &>,
                     core::Task<core::Expected<>>>
    [[nodiscard]] bool route(const std::string &path, Handler &&handler) {
        // 升级请求先由 HTTP 侧接住：它要回 101 和握手头，真正的 WS 处理
        // 在 http::Server 里等握手发完之后再接管
        const bool ok =
            route("GET",
                  path,
                  [](Request &req) -> core::Task<core::Expected<Response>> {
                      Response res;
                      if (req.header("upgrade") == "websocket") {
                          res.with_status(101);
                      }
                      co_return res;
                  });

        return ok && ws_tree_.insert(path, std::move(handler));
    }

    WsHandler get_ws_handler(const std::string &path) {
        auto match = ws_tree_.search(path);
        return match.value ? *match.value : nullptr;
    }

    core::Task<core::Expected<Response>> dispatch(Request &req);

    Router() = default;
    Router(Router &&) = default;
    ~Router() = default;

  private:
    std::unordered_map<std::string, RadixTree<HttpHandler>> trees_;
    RadixTree<WsHandler> ws_tree_;
};

inline core::Task<core::Expected<Response>> Router::dispatch(Request &req) {
    RadixTree<HttpHandler>::normalize_in_place(req.url_path);

    if (auto it = trees_.find(req.method); it != trees_.end()) {
        if (auto match = it->second.search(req.url_path); match.value) {
            req.path_params = std::move(match.params);
            co_return co_await (*match.value)(req);
        }
    }

    co_return Response{}.with_status(404).with_body("<h1>404 Not Found</h1>");
}

} // namespace netx::http::details