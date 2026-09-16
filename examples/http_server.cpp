// netx_http —— HTTP 服务示例：路由、:name 路径参数、静态文件。
//
//   ./netx_http [最大并发连接数]        # 默认 http://127.0.0.1:8080/，不限并发
//
// 静态文件从 examples/public 取，路径由 NETX_WEB_SRC_DIR 在编译期传入。

#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/http/server.hpp"

#include <chrono>
#include <cstdlib>
#include <format>
#include <string>

using netx::core::Expected;
using netx::core::Task;
using netx::http::Request;
using netx::http::Response;
using netx::http::Server;

int main(int argc, char **argv) {
    // 0 表示不限。上限一旦设了，超出部分的连接会被接进来立刻关掉 ——
    // 对端马上拿到 RST，比让它在内核 backlog 里干等更早得到反馈。
    const size_t max_conns =
        (argc > 1) ? std::strtoul(argv[1], nullptr, 10) : 0;

    auto &server = Server::server();

    server
        .listen("0.0.0.0", 8080)
        // 首页
        .route("GET",
               "/",
               [](Request &) -> Task<Expected<Response>> {
                   co_return Response{}
                       .with_status(200)
                       .content_type("text/html; charset=utf-8")
                       .with_body("<h1>Hello NetX</h1>"
                                  "<p><a href=\"/hello/world\">/hello/world</a>"
                                  " &middot; <a href=\"/about.html\">"
                                  "/about.html</a></p>");
               })
        // 路径参数：/hello/:name 里的 name 可以用 req.path("name") 取到
        .route("GET",
               "/hello/:name",
               [](Request &req) -> Task<Expected<Response>> {
                   co_return Response{}
                       .with_status(200)
                       .content_type("text/plain; charset=utf-8")
                       .with_body(std::format("Hello, {}!", req.path("name")));
               })
        // 查询串：/echo?q=...&n=...
        .route("GET",
               "/echo",
               [](Request &req) -> Task<Expected<Response>> {
                   co_return Response{}
                       .with_status(200)
                       .content_type("text/plain; charset=utf-8")
                       .with_body(std::format("q={} n={} ua={}",
                                              req.query("q"),
                                              req.query("n"),
                                              req.header("user-agent")));
               })
        // POST 的表单/正文
        .route("POST",
               "/echo",
               [](Request &req) -> Task<Expected<Response>> {
                   co_return Response{}
                       .with_status(200)
                       .content_type("text/plain; charset=utf-8")
                       .with_body(std::format(
                           "{} bytes: {}", req.body.size(), req.body));
               })
        // 通配符兜底：剩下的都当静态文件
        //
        // 路径到达这里之前已经被归一化过（"." / ".." / 重复斜杠都折掉了），
        // 所以拼接不会跳出 NETX_WEB_SRC_DIR；文件不存在时 Sender 回 404。
        .route("GET", "/*", [](Request &req) -> Task<Expected<Response>> {
            co_return Response{}.with_status(200).with_file(
                std::string{NETX_WEB_SRC_DIR} + req.url_path);
        });

    // 必须在 start() 之前调用：并发名额是在配置时建好的
    if (max_conns > 0) {
        server.max_connections(max_conns);
    }

    server.timeout(std::chrono::seconds(30)).loop(4).start();
}
