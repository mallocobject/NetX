<div align="center">
  <pre>
███╗   ██╗███████╗████████╗██╗  ██╗
████╗  ██║██╔════╝╚══██╔══╝╚██╗██╔╝
██╔██╗ ██║█████╗     ██║    ╚███╔╝ 
██║╚██╗██║██╔══╝     ██║    ██╔██╗ 
██║ ╚████║███████╗   ██║   ██║  ██║
╚═╝  ╚═══╝╚══════╝   ╚═╝   ╚═╝  ╚═╝
  </pre>

  <p align="center">
    <img src="https://img.shields.io/badge/C%2B%2B-23-blue.svg" alt="C++23">
    <img src="https://img.shields.io/badge/platform-Linux-red.svg" alt="Platform">
    <img src="https://img.shields.io/badge/build-CMake-blueviolet.svg" alt="CMake">
    <img src="https://img.shields.io/badge/status-Developing-green.svg" alt="Developing">
  </p>

  <p align="center"> <b>NETX - High Performance Network Framework</b></p>
</div>

---

NetX is a header-only C++23 coroutine network framework for Linux, built on
epoll. It ships with an HTTP/WebSocket server — the
only third-party dependency is OpenSSL (WebSocket handshake).

## Build

```bash
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build -j                    # elog, netx, netx_test and all examples

cmake -S examples -B examples/build -DCMAKE_CXX_COMPILER=g++
cmake --build examples/build -j           # examples only
```

The build stops with an explicit message if the compiler is too old, rather
than failing later on a missing `<print>`. `CMAKE_BUILD_TYPE` defaults to
`Release`; pass `-DCMAKE_BUILD_TYPE=Debug` for a debug build.

## Quick Start

```cpp
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/http/server.hpp"

using namespace netx::core;
using namespace netx::http;
using namespace std::chrono_literals;

int main() {
    Server::server()
        .listen("127.0.0.1", 8080)
        .route("GET",
               "/",
               [](Request &) -> Task<Expected<Response>> {
                   co_return Response{}
                       .with_status(200)
                       .content_type("text/html")
                       .with_body("<h1>Hello NetX</h1>");
               })
        .max_connections(1024) // 0 或缺省表示不限
        .timeout(3s)
        .loop(8)
        .start();
}
```

Routes support `:param` and `*` wildcards (`/users/:id`, `/*`). Handlers read
the request via `req.header/query/path/body` and build the reply with
`Response{}.with_status().with_body().with_file()`.

Beyond the limit, connections are accepted and closed immediately, so the
peer gets prompt feedback rather than waiting in the kernel backlog.

Core coroutine primitives: `Task<T>`, `Expected<T>`, `sleep`, `when_any`,
`co_spawn`, `async_main`.

## WebSocket

```cpp
.route("/ws", [](websocket::details::Connection &conn) -> Task<Expected<>> {
    while (true) {
        auto frame = co_await conn.receive();
        if (!frame) {
            break; // 对端关闭，或帧不合法
        }
        switch (frame->opcode) {
        case websocket::details::Opcode::kText:
            co_await co_await conn.send_text(frame->payload);
            break;
        case websocket::details::Opcode::kPing:
            co_await co_await conn.send_pong(frame->payload);
            break;
        case websocket::details::Opcode::kClose:
            co_await co_await conn.send_close({});
            co_return {};
        default:
            break; // 续帧：调用方自行重组
        }
    }
    co_return {};
})
```

`Connection` provides `receive()` / `send_text()` / `send_binary()` /
`send_ping()` / `send_pong()` / `send_close()`. The upgrade handshake
(101 + `Sec-WebSocket-Accept`) is handled automatically.

## Examples

| Target | Description |
| --- | --- |
| `netx_http` | HTTP server: routes, `:name` path params, static files (8080) |
| `netx_ws` | WebSocket echo + chat page `public/ws_chat.html` (8081) |
| `netx_tcp_echo` | Raw TCP echo on `net::details::Server`, the layer below HTTP (8082) |

```bash
./examples/build/netx_http        # http://127.0.0.1:8080/
./examples/build/netx_http 1000   # same, but at most 1000 concurrent clients
./examples/build/netx_ws          # http://127.0.0.1:8081/
./examples/build/netx_tcp_echo    # echo | nc 127.0.0.1 8082
```
