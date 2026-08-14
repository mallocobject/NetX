#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/http/server.hpp"
#include "netx/websocket/connection.hpp"
#include "netx/websocket/frame.hpp"

using namespace netx::core;
using namespace netx::http;

namespace ws = netx::websocket::details;

int main()
{
	Server::server()
		.listen("0.0.0.0", 8081)
		// 聊天页面
		.route("GET", "/",
			   [](Request&) -> Task<Expected<Response>>
			   {
				   co_return Response{}
					   .with_status(200)
					   .content_type("text/html")
					   .with_file(NETX_WEB_SRC_DIR "/ws_chat.html");
			   })
		// WebSocket echo:收到什么就回什么
		.route("/ws",
			   [](ws::Connection& conn) -> Task<Expected<>>
			   {
				   while (true)
				   {
					   auto frame_exp = co_await conn.receive();
					   if (!frame_exp)
					   {
						   break; // 连接断开
					   }

					   ws::Frame& frame = frame_exp.value();
					   switch (frame.opcode)
					   {
					   case ws::Opcode::kText:
						   if (auto exp =
								   co_await conn.send_text(frame.payload);
							   !exp)
						   {
							   co_return {};
						   }
						   break;
					   case ws::Opcode::kBinary:
						   if (auto exp =
								   co_await conn.send_binary(frame.payload);
							   !exp)
						   {
							   co_return {};
						   }
						   break;
					   case ws::Opcode::kPing:
						   if (auto exp =
								   co_await conn.send_pong(frame.payload);
							   !exp)
						   {
							   co_return {};
						   }
						   break;
					   case ws::Opcode::kClose:
						   co_await conn.send_close({});
						   co_return {};
					   default:
						   break;
					   }
				   }
				   co_return {};
			   })
		.loop(4)
		.start();
}
