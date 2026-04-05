#include "netx/http/response.hpp"
#include "netx/http/server.hpp"
#include <utility>

using namespace netx::http;
using namespace netx::net;
using namespace netx::async;
using namespace netx::ws;
using namespace std::chrono_literals;

int main()
{
	HttpServer::server()
		.listen("127.0.0.1", 8080)
		.route("GET", "/",
			   [](HttpRequest* req) -> Task<HttpResponse> {
				   co_return std::move(
					   HttpResponse{}.file(NETX_WEB_SRC_DIR "/index.html"));
			   })
		.ws("/chat/:id",
			[](WSConnection* ws) -> Task<>
			{
				while (true)
				{
					auto frame = co_await ws->receive();
					if (!frame)
					{
						break;
					}
					if (frame->opcode == WSOpcode::kClose)
					{
						elog::LOG_INFO(
							"WS received Close Frame. Sending response...");
						co_await ws->send_frame(WSOpcode::kClose, "");
						break;
					}

					// 情况 3：正常文本消息
					if (frame->opcode == WSOpcode::kText)
					{
						co_await ws->send_text("Echo: " + frame->payload);
					}
				}
			})
		.loop(8)
		.start();
}