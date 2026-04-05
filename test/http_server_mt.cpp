#include "netx/http/response.hpp"
#include "netx/http/server.hpp"
#include <utility>

using namespace netx::http;
using namespace netx::net;
using namespace netx::async;
using namespace std::chrono_literals;

int main()
{
	HttpServer::server()
		.listen("127.0.0.1", 8080)
		.route("GET", "/",
			   [](HttpRequest* req) -> Task<HttpResponse>
			   {
				   co_return std::move(HttpResponse{}
										   .status(200)
										   .content_type("text/html")
										   .body("<h1>Hello Netx</h1>"));
			   })
		.timeout(3s)
		.loop(8)
		.start();
}