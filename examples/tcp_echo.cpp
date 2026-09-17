// netx_tcp_echo —— 直接建在 net::details::Server 上的 TCP echo 服务。
//
//   echo hello | nc 127.0.0.1 8082
//
// 这一层在 HTTP 之下：没有报文解析，只把收到的字节原样写回去。派生类必须
// 提供 handle_client，基类在 accept 之后把连接交给它。

#include "elog/logger.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/net/server.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"

#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace net = netx::net::details;

struct TcpEchoServer : net::Server {
    // 基类通过 self.handle_client(...) 调用下面那个私有实现，所以要放行
    friend class net::Server;

    static TcpEchoServer &instance() {
        // 预留 fd 传 -1：start() 会自己开两个 /dev/null 备用，
        // EMFILE 时的自救才有东西可关
        static TcpEchoServer server{make_listen_stream(), -1, -1};
        return server;
    }

  private:
    /// 建监听用的 Stream。失败就抛 std::system_error —— 单例工厂没有错误
    /// 通道，而这里失败等于服务起不来。
    static net::Stream make_listen_stream() {
        auto fd = net::Socket::socket();
        if (!fd) {
            throw std::system_error{fd.error()};
        }

        auto stream = net::Stream::create(*fd);
        if (!stream) {
            const auto ec = stream.error();
            (void)net::Socket::close(*fd);
            throw std::system_error{ec};
        }
        return std::move(*stream);
    }

    netx::core::Task<netx::core::Expected<>> handle_client(int read_fd,
                                                           int write_fd) {
        auto stream = net::Stream::create(read_fd, write_fd);
        if (!stream) {
            (void)net::Socket::close(read_fd);
            (void)net::Socket::close(write_fd);
            co_return netx::core::details::make_error_to_unexpected(
                netx::core::details::Error::InvalidOperation);
        }

        net::Stream conn = std::move(*stream);
        elog::LOG_INFO("echo: client connected");

        while (true) {
            auto n = co_await conn.read();
            if (!n) {
                break; // 对端关闭或读失败
            }

            const std::string data = conn.read_buf.retrieve_string(*n);
            if (auto w = co_await conn.write(data); !w) {
                break;
            }
        }

        co_await conn.shutdown();
        elog::LOG_INFO("echo: client disconnected");
        co_return {};
    }

    TcpEchoServer(net::Stream &&stream, int idle_fd1, int idle_fd2)
        : net::Server(std::move(stream), idle_fd1, idle_fd2) {
    }
};

int main() {
    TcpEchoServer::instance().listen("0.0.0.0", 8082).loop(4).start();
}
