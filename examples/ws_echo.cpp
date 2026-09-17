// netx_ws —— WebSocket 回显示例，附带一个浏览器聊天页。
//
//   ./netx_ws             # 打开 http://127.0.0.1:8081/
//
// 页面连上 /ws 后，发什么都会原样回来（文本、二进制、ping 都处理）。

#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/http/server.hpp"
#include "netx/websocket/connection.hpp"
#include "netx/websocket/frame.hpp"

#include <string>

using netx::core::Expected;
using netx::core::Task;
using netx::http::Request;
using netx::http::Response;
using netx::http::Server;

namespace ws = netx::websocket::details;

int main() {
    Server::server()
        .listen("0.0.0.0", 8081)
        // 聊天页
        .route("GET",
               "/",
               [](Request &) -> Task<Expected<Response>> {
                   co_return Response{}.with_status(200).with_file(
                       NETX_WEB_SRC_DIR "/ws_chat.html");
               })
        // WebSocket 回显
        .route("/ws",
               [](ws::Connection &conn) -> Task<Expected<>> {
                   while (true) {
                       auto frame = co_await conn.receive();
                       if (!frame) {
                           break; // 对端关闭或帧不合法
                       }

                       switch (frame->opcode) {
                       case ws::Opcode::kText:
                           co_await co_await conn.send_text(frame->payload);
                           break;
                       case ws::Opcode::kBinary:
                           co_await co_await conn.send_binary(frame->payload);
                           break;
                       case ws::Opcode::kPing:
                           // 按 RFC 必须回 pong，载荷原样带回
                           co_await co_await conn.send_pong(frame->payload);
                           break;
                       case ws::Opcode::kClose:
                           co_await co_await conn.send_close({});
                           co_return {};
                       default:
                           break; // 续帧：这个例子不做重组
                       }
                   }
                   co_return {};
               })
        .loop(4)
        .start();
}
