#include "netx/http/server.hpp"
#include <csignal>
#include <string>

using namespace netx::http;
using namespace netx::net;
using namespace netx::async;
using namespace std::chrono_literals;

int main()
{
	HttpServer::server()
		.listen("127.0.0.1", 8080)
		.route(
            "GET", "/",
            [](const HttpRequest& req, HttpResponse* res, Stream* stream) -> Task<>
            {
                res->set_status(200);
                res->set_content_type("text/html");
                res->keep_alive(
                    req.header("connection") != "close" &&
                    !(req.version == "HTTP/1.0" &&
                    req.header("connection") != "keep-alive"));
                res->set_body("<h1>Hello Netx</h1>");

                co_await stream->write(res->to_formatted_string());
            })
        .timeout(3s)
		.loop(8)
		.start();
}