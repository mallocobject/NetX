#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/http/server.hpp"

using namespace netx::core;
using namespace netx::http;
using namespace std::chrono_literals;

int main()
{
	Server::server()
		.listen("127.0.0.1", 8080)
		.route("GET", "/",
			   [](Request& req) -> Task<Expected<Response>>
			   {
				   co_return Response{}
					   .with_status(200)
					   .content_type("text/html")
					   .with_body("<h1>Hello NetX</h1>");
			   })
		.timeout(3s)
		.loop(8)
		.start();
}