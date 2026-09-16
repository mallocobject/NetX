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
#include <utility>
namespace netx {
namespace http {
class Server : public net::details::Server {
    // 基类通过 self.handle_client(...) 调用下面那个私有实现，所以要放行。
    // CRTP 时代是 Server<Server>，现在基类不再是模板。
    friend class net::details::Server;
    Server(net::details::Stream &&stream, int idle_fd1, int idle_fd2)
        : net::details::Server(std::move(stream), idle_fd1, idle_fd2) {
    }

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
            elog::LOG_ERROR("route {} {}: parameter name conflicts with an "
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
            elog::LOG_ERROR("route {}: parameter name conflicts with an "
                            "existing route",
                            path);
        }
        return *this;
    }

  private:
    /// 建监听用的 Stream。失败就抛 std::system_error —— 单例工厂没有错误
    /// 通道，sticky_error_ 也得先有对象才记得到；这里失败等于进程起不来，
    /// 抛一个带错误码的异常，比 .value() 的 bad_expected_access 清楚。
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

    /// 归还并发名额。用 RAII 保证每条退出路径都还 —— 漏还会让名额只减
    /// 不增，最终拒绝掉所有新连接。
    /// 注意持有的是 Server& 而不是 net::details::Server&：protected 成员
    /// 必须经由派生类访问，写成基类引用会被编译期挡下。
    struct SlotGuard {
        Server &srv;
        ~SlotGuard() {
            srv.release_connection();
        }
    };

    core::Task<core::Expected<>> handle_client(int read_fd, int write_fd);

  private:
    details::Router router_{};
};

inline core::Task<core::Expected<>> Server::handle_client(int read_fd,
                                                          int write_fd) {

    elog::LOG_DEBUG("handle_client start fd={}", read_fd);
    auto stream_exp = net::details::Stream::create(read_fd, write_fd);
    if (!stream_exp) {
        elog::LOG_DEBUG("handle_client Stream::create failed fd={}", read_fd);
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
                // 没配超时就不必把 sleep 拉进来赛跑。默认那个"不超时"的哨兵
                // 每读一次都要挂一个一年期的定时器、读完再取消掉，纯属白做。
                read_exp = co_await s.read();
            }

            if (idle_timeout) {
                elog::LOG_DEBUG("Connection idle timeout, closing fd {}",
                                s.read_fd);
                co_await s.write(Response{}
                                     .with_status(408)
                                     .keep_alive(false)
                                     .to_formatted_string());
                break;
            }

            if (!read_exp) {
                if (read_exp.error() != core::details::Error::BrokenPipe) {
                    elog::LOG_DEBUG("Client read error: {}",
                                    read_exp.error().message());
                }
                break;
            }

            bool should_close = false;
            while (s.read_buf.readable_bytes() > 0) {
                if (!session.parse(s.read_buf)) {
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
                        std::string accept_key = websocket::details::
                            WSHandshake::generate_accept_key(client_key);

                        res.with_header("Upgrade", "websocket")
                            .with_header("Connection", "Upgrade")
                            .with_header("Sec-WebSocket-Accept", accept_key);

                        if (auto exp = co_await details::Sender::send(s, res);
                            !exp) {
                            const std::error_code &ec = exp.error();
                            elog::LOG_ERROR("{}, {}", ec.value(), ec.message());
                            break;
                        }

                        auto ws_handler = router_.get_ws_handler(req.url_path);
                        if (ws_handler) {
                            websocket::details::Connection ws_conn(s);
                            co_await ws_handler(ws_conn); // 进入长连接处理循环
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
                    if (!send_res || !is_keep) {
                        should_close = true;
                        break;
                    }

                    session.clear();
                    s.read_buf.try_shrink();
                    s.write_buf.try_shrink();
                } else {
                    break;
                }
            }
            if (should_close) {
                break;
            }
        }
    } catch (const std::exception &e) {
        elog::LOG_ERROR("Exception in handleClient: {}", e.what());
    }

    co_await s.shutdown();
    elog::LOG_DEBUG("handle_client end fd={}", read_fd);
    co_return {};
}
} // namespace http
} // namespace netx