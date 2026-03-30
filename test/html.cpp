#include "netx/http/server.hpp"

using namespace netx::http;
using namespace netx::net;
using namespace netx::async;
using namespace std::chrono_literals;

int main()
{
	HttpServer::server()
		.listen("127.0.0.1", 8080)
		.route("GET", "/",
			   [](const HttpRequest& req, HttpResponse* res,
				  Stream* stream) -> Task<>
			   {
				   std::string html = R"HTML(
					<!DOCTYPE html>
					<html>
					<head>
						<title>Netx Demo</title>
						<meta charset="UTF-8">
						<style>
							button { font-size: 20px; padding: 10px 20px; }
							#result { margin-top: 20px; font-size: 18px; }
						</style>
					</head>
					<body>
						<h1>你好, Netx</h1>
						<button id="clickBtn">点击请求后台</button>
						<div id="result"></div>

						<script>
							document.getElementById('clickBtn').addEventListener('click', function() {
								fetch('/api/click', {
									method: 'POST',
									headers: { 'Content-Type': 'application/json' }
								})
								.then(response => response.json())
								.then(data => {
									document.getElementById('result').innerHTML = '后台返回: ' + data.message;
								})
								.catch(error => {
									console.error('Error:', error);
									document.getElementById('result').innerHTML = '请求失败';
								});
							});
						</script>
					</body>
					</html>
					)HTML";

				   res->status(200)
					   .content_type("text/html; charset=utf-8")
					   .keep_alive(req.header("connection") != "close" &&
								   !(req.version == "HTTP/1.0" &&
									 req.header("connection") != "keep-alive"))
					   .body(html);

				   co_await stream->write(res->to_formatted_string());
			   })
		.route(
			"POST", "/api/click",
			[](const HttpRequest& req, HttpResponse* res,
			   Stream* stream) -> Task<>
			{
				std::string json =
					R"({"message": "来自 C++ 服务器的问候！", "timestamp": 123456789})";
				res->status(200)
					.content_type("application/json; charset=utf-8")
					.keep_alive(true)
					.body(json);

				co_await stream->write(res->to_formatted_string());
			})
		.timeout(3s)
		.loop(8)
		.start();
}