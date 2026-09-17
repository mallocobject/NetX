#pragma once

#include "elog/logger.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/sleep.hpp"
#include "netx/core/when_any.hpp"
#include "netx/http/router.hpp"
#include "netx/http/sender.hpp"
#include "netx/http/session.hpp"
#include "netx/net/server.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"
#include "netx/websocket/handshake.hpp"
#include <cassert>
#include <cstddef>
#include <string_view>
#include <utility>

namespace netx::http {
class Server : public net::details::Server {
    friend class net::details::Server; // 基类通过 self.handle_client(...)
                                       // 调用下面那个私有实现

  public:
    static Server &server() {
        static Server http_server{make_listen_stream(),
                                  /*idle_fd1=*/-1,
                                  /*idle_fd2=*/-1};
        return http_server;
    }

    template <typename Handler>
        requires std::invocable<Handler, Request &> &&
                 std::same_as<std::invoke_result_t<Handler, Request &>,
                              core::Task<core::Expected<Response>>>
    Server &route(const std::string &method,
                  const std::string &path,
                  Handler &&handler) {
        if (!router_.route(method, path, std::forward<Handler>(handler))) {
            // 注册失败只有一种原因：同一位置已经有不同名字的参数段
            // （/users/:id 与 /users/:name）。这是写错了路由，不能静默吞掉
            sticky_error_ = core::details::make_error_to_unexpected(
                                core::details::Error::InvalidOperation)
                                .error();
            elog::LOG_FATAL("route {} {}: parameter name conflicts with an "
                            "existing route",
                            method,
                            path);
        }
        return *this;
    }

    template <typename Handler>
        requires std::invocable<Handler, websocket::details::Connection &> &&
                 std::same_as<
                     std::invoke_result_t<Handler,
                                          websocket::details::Connection &>,
                     core::Task<core::Expected<>>>
    Server &route(const std::string &path, Handler &&handler) {
        if (!router_.route(path, std::forward<Handler>(handler))) {
            sticky_error_ = core::details::make_error_to_unexpected(
                                core::details::Error::InvalidOperation)
                                .error();
            elog::LOG_FATAL("route {}: parameter name conflicts with an "
                            "existing route",
                            path);
        }
        return *this;
    }

  private:
    struct SlotGuard {
        Server &srv;
        ~SlotGuard() {
            srv.release_connection();
        }
    };

    Server(net::details::Stream &&stream, int idle_fd1, int idle_fd2)
        : net::details::Server(std::move(stream), idle_fd1, idle_fd2) {
    }

    static net::details::Stream make_listen_stream() {
        auto fd = net::details::Socket::socket();
        if (!fd) {
            throw std::system_error{fd.error()};
        }

        auto stream = net::details::Stream::create(*fd);
        if (!stream) {
            const auto ec = stream.error();
            (void)net::details::Socket::close(*fd);
            throw std::system_error{ec};
        }
        return std::move(*stream);
    }

    core::Task<core::Expected<>> handle_client(int read_fd, int write_fd);

  private:
    details::Router router_{};
};

inline core::Task<core::Expected<>> Server::handle_client(int read_fd,
                                                          int write_fd) {

    elog::LOG_DEBUG("new connection fd={}", read_fd);

    std::string_view close_reason = "unknown";
    size_t requests_served = 0;

    auto stream_exp = net::details::Stream::create(read_fd, write_fd);
    if (!stream_exp) {
        elog::LOG_WARN("connection setup failed fd={}", read_fd);
        ::close(read_fd);
        ::close(write_fd);
        co_return {};
    }

    net::details::Stream s = std::move(stream_exp.value());

    const SlotGuard guard{*this};
    details::Session session{};

    try {
        while (true) {
            core::Expected<size_t> read_exp;
            bool idle_timeout = false;

            if (has_timeout()) {
                auto raced =
                    co_await core::when_any(s.read(), core::sleep(timeout_));
                if (!raced) {
                    co_return std::unexpected{raced.error()};
                }
                if (raced->index() == 1) {
                    idle_timeout = true;
                } else {
                    read_exp = std::move(std::get<0>(*raced));
                }
            } else {
                read_exp = co_await s.read();
            }

            if (idle_timeout) {
                close_reason = "idle timeout";
                co_await s.write(Response{}
                                     .with_status(408)
                                     .keep_alive(false)
                                     .to_formatted_string());
                break;
            }

            if (!read_exp) {
                // 对端正常关闭也走这里（Buffer 把 EOF 报成 BrokenPipe），
                // 那不是错误，不必单记一条
                if (read_exp.error() == core::details::Error::BrokenPipe) {
                    close_reason = "peer closed";
                } else {
                    close_reason = "read error";
                    elog::LOG_DEBUG("read error fd={}: {}",
                                    read_fd,
                                    read_exp.error().message());
                }
                break;
            }

            bool should_close = false;
            while (s.read_buf.readable_bytes() > 0) {
                if (!session.parse(s.read_buf)) {
                    close_reason = "bad request";
                    // co_await s.write("HTTP/1.1 400 Bad Request\r\nConnection:
                    // " 				 "close\r\n\r\n");
                    co_await s.write(Response{}
                                         .with_status(400)
                                         .keep_alive(false)
                                         .to_formatted_string());
                    should_close = true;
                    break;
                }

                if (session.completed()) {
                    auto &req = session.req();

                    auto res_exp = co_await router_.dispatch(req);

                    if (!res_exp) {
                        close_reason = "handler error";
                        const std::error_code &ec = res_exp.error();
                        elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
                        // co_await s.write(
                        // 	"HTTP/1.1 500 Internal Server Error\r\nConnection: "
                        // 	"close\r\n\r\n");
                        co_await s.write(Response{}
                                             .with_status(500)
                                             .keep_alive(false)
                                             .to_formatted_string());
                        should_close = true;
                        break;
                    }

                    Response res = std::move(res_exp.value());
                    if (res.status_code == 101) {
                        std::string client_key{req.header("sec-websocket-key")};
                        auto accept_key = websocket::details::WSHandshake::
                            generate_accept_key(client_key);

                        if (!accept_key) {
                            close_reason = "accept key error";
                            elog::LOG_ERROR("websocket accept key failed: {}",
                                            accept_key.error().message());
                            co_await s.write(Response{}
                                                 .with_status(500)
                                                 .keep_alive(false)
                                                 .to_formatted_string());
                            should_close = true;
                            break;
                        }

                        res.with_header("Upgrade", "websocket")
                            .with_header("Connection", "Upgrade")
                            .with_header("Sec-WebSocket-Accept", *accept_key);

                        if (auto exp = co_await details::Sender::send(s, res);
                            !exp) {
                            elog::LOG_DEBUG("send failed: {}",
                                            exp.error().message());
                            break;
                        }

                        auto ws_handler = router_.get_ws_handler(req.url_path);
                        if (ws_handler) {
                            websocket::details::Connection ws_conn(s);
                            // 进入长连接处理循环。它的结果不能丢：handler 的
                            // 错误是这里唯一能记下来的地方
                            const auto ws_res = co_await ws_handler(ws_conn);
                            if (!ws_res) {
                                elog::LOG_WARN("websocket handler error: {}",
                                               ws_res.error().message());
                            }
                        }
                        co_return {};
                    }

                    bool is_keep = (req.header("connection") != "close");
                    if (req.version == "HTTP/1.0" &&
                        req.header("connection") != "keep-alive") {
                        is_keep = false;
                    }
                    res.keep_alive(is_keep);

                    auto send_res = co_await details::Sender::send(s, res);
                    if (!send_res) {
                        elog::LOG_DEBUG("send failed: {}",
                                        send_res.error().message());
                    }
                    if (!send_res || !is_keep) {
                        close_reason =
                            send_res ? "keep-alive off" : "send failed";
                        should_close = true;
                        break;
                    }

                    ++requests_served;
                    session.clear();
                    s.read_buf.try_shrink();
                    s.write_buf.try_shrink();
                } else {
                    // 报文还没收全，等下一轮数据
                    break;
                }
            }
            if (should_close) {
                break;
            }
        }
    } catch (const std::exception &e) {
        close_reason = "exception";
        elog::LOG_ERROR("Exception in handleClient: {}", e.what());
    }

    co_await s.shutdown();
    elog::LOG_DEBUG("connection closed fd={} reason={} requests={}",
                    read_fd,
                    close_reason,
                    requests_served);
    co_return {};
}
} // namespace netx::http