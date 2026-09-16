// netx/http 的纯逻辑部分测试。
//
// 挑的是不依赖 socket/事件循环、能真正断言行为的三块：
//   parser.hpp     —— HTTP 请求解析（状态机，最该被测）
//   radix_tree.hpp —— 路由匹配与路径归一化（安全相关）
//   response.hpp   —— 响应头拼装

#include "netx/http/parser.hpp"
#include "netx/http/radix_tree.hpp"
#include "netx/http/response.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using netx::http::Response;
using netx::http::details::Parser;
using netx::http::details::RadixTree;

// 解析器的别名；上限用例里反复用到
using P = netx::http::details::Parser;

// ------------------------------------------------------------------ Parser

namespace {

bool parse_all(Parser &p, const std::string &raw) {
    return p.parse(raw);
}

} // namespace

TEST_CASE("解析最简单的 GET 请求", "[http][parser]") {
    Parser p;
    REQUIRE(parse_all(p, "GET /hello HTTP/1.1\r\nHost: example.com\r\n\r\n"));

    CHECK(p.completed());
    CHECK(p.req.method == "GET");
    CHECK(p.req.url_path == "/hello");
    CHECK(p.req.version == "HTTP/1.1");
    CHECK(p.req.header("host") == "example.com");
}

TEST_CASE("header 的键统一转小写，取值时不必关心大小写", "[http][parser]") {
    Parser p;
    REQUIRE(parse_all(
        p,
        "GET / HTTP/1.1\r\nContent-Type: text/plain\r\nX-Custom: v\r\n\r\n"));

    CHECK(p.req.header("content-type") == "text/plain");
    CHECK(p.req.header("x-custom") == "v");
    // 存的就是小写键
    CHECK(p.req.header_params.count("Content-Type") == 0);
}

TEST_CASE("解析查询串", "[http][parser]") {
    Parser p;
    REQUIRE(parse_all(p, "GET /search?q=abc&page=2 HTTP/1.1\r\n\r\n"));

    CHECK(p.req.url_path == "/search");
    CHECK(p.req.query("q") == "abc");
    CHECK(p.req.query("page") == "2");
    CHECK(p.req.query("nope").empty());
}

TEST_CASE("按 Content-Length 解析 body", "[http][parser]") {
    Parser p;
    REQUIRE(parse_all(p,
                      "POST /submit HTTP/1.1\r\nContent-Length: 5\r\n\r\n"
                      "hello"));

    CHECK(p.completed());
    CHECK(p.req.method == "POST");
    CHECK(p.req.body == "hello");
}

TEST_CASE("body 分多次喂入也能解析完", "[http][parser]") {
    Parser p;
    REQUIRE(p.parse("POST /submit HTTP/1.1\r\nContent-Length: 10\r\n\r\n"));
    CHECK_FALSE(p.completed()); // 头解析完，body 还差 10 字节

    REQUIRE(p.parse("01234"));
    CHECK_FALSE(p.completed());

    REQUIRE(p.parse("56789"));
    CHECK(p.completed());
    CHECK(p.req.body == "0123456789");
}

TEST_CASE("请求行不合规时报错", "[http][parser]") {
    // 状态行后面必须有 \n
    Parser a;
    CHECK_FALSE(a.parse("GET / HTTP/1.1\rX"));

    // header 行后面同理
    Parser b;
    CHECK_FALSE(b.parse("GET / HTTP/1.1\r\nHost: x\rX"));

    // 首个字符必须是字母
    Parser c;
    CHECK_FALSE(c.parse("1ET / HTTP/1.1\r\n"));
}

TEST_CASE("method 有长度上限，且不允许出现 CR/LF", "[http][parser]") {
    // 超长
    P a;
    CHECK_FALSE(a.parse("GETTTTTTTTTTTTTTTTTTTTTTTTTT / HTTP/1.1\r\n\r\n"));

    // 请求行里没有空格、直接 CRLF：method 里出现 CR 就必须报错。
    // 修之前这里会一路吞下去 —— 既不返回 400，连接也不会被关掉，
    // 客户端只能干等（实测 1MB 垃圾打过去，服务端毫无反应）。
    P b;
    CHECK_FALSE(b.parse("AAAAAAAAAA\r\n\r\n"));

    // 正常方法仍然通过
    P c;
    CHECK(c.parse("DELETE /x HTTP/1.1\r\n\r\n"));
    CHECK(c.req.method == "DELETE");
}

TEST_CASE("header 键、值、条数都有上限", "[http][parser]") {
    // 键超长
    P a;
    std::string big_key = "GET / HTTP/1.1\r\nX-";
    big_key += std::string(Parser::kMaxHeaderKeyLen + 10, 'k');
    big_key += ": v\r\n\r\n";
    CHECK_FALSE(a.parse(big_key));

    // 值超长
    P b;
    std::string big_val = "GET / HTTP/1.1\r\nX-K: ";
    big_val += std::string(Parser::kMaxHeaderValueLen + 10, 'v');
    big_val += "\r\n\r\n";
    CHECK_FALSE(b.parse(big_val));

    // 条数超限。注意键必须互不相同 —— 用同一个键的话 unordered_map 的
    // size 根本不会涨，这条用例就变成空转了
    P c;
    std::string many = "GET / HTTP/1.1\r\n";
    for (size_t i = 0; i < Parser::kMaxHeaderCount + 5; ++i) {
        many += "X-H" + std::to_string(i) + ": v\r\n";
    }
    many += "\r\n";
    CHECK_FALSE(c.parse(many));

    // 刚好在限内的仍然通过
    P d;
    std::string ok = "GET / HTTP/1.1\r\n";
    for (size_t i = 0; i < Parser::kMaxHeaderCount; ++i) {
        ok += "X-H" + std::to_string(i) + ": v\r\n";
    }
    ok += "\r\n";
    CHECK(d.parse(ok));
    CHECK(d.completed());
}

TEST_CASE("查询串键与值也有上限", "[http][parser]") {
    P a;
    std::string long_key = "GET /?";
    long_key += std::string(Parser::kMaxQueryKeyLen + 10, 'k');
    long_key += "=v HTTP/1.1\r\n\r\n";
    CHECK_FALSE(a.parse(long_key));

    P b;
    std::string long_val = "GET /?k=";
    long_val += std::string(Parser::kMaxQueryValueLen + 10, 'v');
    long_val += " HTTP/1.1\r\n\r\n";
    CHECK_FALSE(b.parse(long_val));
}

TEST_CASE("超过上限的 body 被拒", "[http][parser]") {
    Parser p;
    // 声明得比上限多 1 字节
    const std::string over = "POST / HTTP/1.1\r\nContent-Length: " +
                             std::to_string(Parser::kMaxBodyLen + 1) +
                             "\r\n\r\n";
    CHECK_FALSE(p.parse(over));
}

TEST_CASE("Content-Length 不是数字时被拒", "[http][parser]") {
    Parser p;
    CHECK_FALSE(p.parse("POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n"));
}

TEST_CASE("超长 url_path 被拒", "[http][parser]") {
    Parser p;
    std::string long_path(1100, 'a');
    CHECK_FALSE(p.parse("GET /" + long_path + " HTTP/1.1\r\n\r\n"));
}

TEST_CASE("路径里的高位字节按普通字节处理", "[http][parser]") {
    // 这一条走的是 kPath 状态，那里不碰 ctype，只做逐字节拼接
    Parser p;
    std::string raw = "GET /";
    raw += static_cast<char>(0xC3); // UTF-8 首字节，作为 char 是负数
    raw += static_cast<char>(0xA9);
    raw += " HTTP/1.1\r\n\r\n";

    CHECK(p.parse(raw));
    CHECK(p.completed());
    CHECK(p.req.url_path.size() == 3);
}

TEST_CASE("首字节是高位字节时被判为非法请求", "[http][parser]") {
    // 这一条才真正走到 std::isalpha：kStart 状态对首字符做字母判断。
    // 0xC3 作为 char 是负数，按标准必须转成 unsigned char 再传 ——
    // 修之前是 UB（glibc 的查表恰好容忍负下标，所以结果看不出差别，
    // 但标准上就是未定义）。
    Parser p;
    std::string raw;
    raw += static_cast<char>(0xC3);
    raw += "ET / HTTP/1.1\r\n\r\n";

    CHECK_FALSE(p.parse(raw)); // 不是字母，拒绝
    CHECK_FALSE(p.completed());
}

TEST_CASE("header 键里的高位字节原样保留", "[http][parser]") {
    // 这一条走到 std::tolower：kHeaderKey 会逐字节转小写。
    // 0xC3 不是大写字母，转小写后应当原样保留。
    Parser p;
    std::string raw = "GET / HTTP/1.1\r\nX-";
    raw += static_cast<char>(0xC3);
    raw += ": v\r\n\r\n";

    REQUIRE(p.parse(raw));
    const std::string key = std::string("x-") + static_cast<char>(0xC3);
    CHECK(p.req.header(key) == "v");
    CHECK(p.req.header_params.size() == 1);
}

// -------------------------------------------------------------- RadixTree

TEST_CASE("静态路由精确匹配", "[http][radix]") {
    RadixTree<int> tree;
    tree.insert("/a/b", 1);
    tree.insert("/a/c", 2);

    CHECK(tree.search("/a/b").value != nullptr);
    CHECK(*tree.search("/a/b").value == 1);
    CHECK(*tree.search("/a/c").value == 2);
    CHECK(tree.search("/a/d").value == nullptr);
    CHECK(tree.search("/a").value == nullptr); // 父节点不是叶子
}

TEST_CASE("根路由 /", "[http][radix]") {
    RadixTree<int> tree;
    tree.insert("/", 7);
    REQUIRE(tree.search("/").value != nullptr);
    CHECK(*tree.search("/").value == 7);
}

TEST_CASE("参数路由 :id 能捕获段并放进 params", "[http][radix]") {
    RadixTree<int> tree;
    tree.insert("/users/:id", 1);

    auto m = tree.search("/users/42");
    REQUIRE(m.value != nullptr);
    CHECK(*m.value == 1);
    CHECK(m.params.at("id") == "42");
}

TEST_CASE("静态段优先于参数段", "[http][radix]") {
    RadixTree<int> tree;
    tree.insert("/users/:id", 1);
    tree.insert("/users/me", 2);

    CHECK(*tree.search("/users/me").value == 2); // 静态优先
    CHECK(*tree.search("/users/99").value == 1); // 落到参数
}

TEST_CASE("通配符 * 吃掉剩余所有段", "[http][radix]") {
    RadixTree<int> tree;
    tree.insert("/static/*", 1);

    REQUIRE(tree.search("/static/a/b/c").value != nullptr);
    CHECK(*tree.search("/static/a/b/c").value == 1);
    REQUIRE(tree.search("/static/x").value != nullptr);
    CHECK(*tree.search("/static/x").value == 1);
    CHECK(tree.search("/other/x").value == nullptr);
}

TEST_CASE("normalize_path 折叠 . 与 ..", "[http][radix]") {
    using Tree = RadixTree<int>;
    CHECK(Tree::normalize_path("/a/./b") == "/a/b");
    CHECK(Tree::normalize_path("/a/b/../c") == "/a/c");
    CHECK(Tree::normalize_path("/a//b") == "/a/b");
    CHECK(Tree::normalize_path("/a/b/") == "/a/b");
    CHECK(Tree::normalize_path("") == "/");
    CHECK(Tree::normalize_path("/") == "/");
}

TEST_CASE("normalize_path 不会让 .. 逃出根目录", "[http][radix]") {
    using Tree = RadixTree<int>;

    // 多出来的 .. 应当被丢弃，而不是在结果里留下上跳
    CHECK(Tree::normalize_path("/../../etc/passwd") == "/etc/passwd");
    CHECK(Tree::normalize_path("/a/../../../x") == "/x");

    for (const char *p : {"/../../etc/passwd", "/a/../../..", "/.."}) {
        const std::string out = Tree::normalize_path(p);
        CHECK(out.starts_with('/'));
        CHECK(out.find("..") == std::string::npos);
    }
}

TEST_CASE("normalize_path 挡住超长路径和内嵌 NUL", "[http][radix]") {
    using Tree = RadixTree<int>;

    CHECK(Tree::normalize_path(std::string(2000, 'a')) == "/");

    std::string with_nul = "/a";
    with_nul += '\0';
    with_nul += "b";
    CHECK(Tree::normalize_path(with_nul) == "/");
}

// ---------------------------------------------------------------- Response

TEST_CASE("响应头包含状态行与 Content-Length", "[http][response]") {
    const Response res =
        Response{}.with_status(200).with_body("hello").keep_alive(true);

    const std::string head = res.to_head_string();
    CHECK(head.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(head.find("Content-Length: 5\r\n") != std::string::npos);
    CHECK(head.find("Connection: keep-alive\r\n") != std::string::npos);
    CHECK(head.ends_with("\r\n\r\n")); // 头与 body 之间必须是空行
}

TEST_CASE("to_formatted_string 把头与 body 拼在一起", "[http][response]") {
    const Response res = Response{}.with_status(200).with_body("hi");
    const std::string all = res.to_formatted_string();

    CHECK(all.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(all.ends_with("\r\n\r\nhi"));
}

TEST_CASE("未知状态码回落到 Unknown", "[http][response]") {
    const Response res = Response{}.with_status(299);
    CHECK(res.to_head_string().starts_with("HTTP/1.1 299 Unknown\r\n"));
}

TEST_CASE("keep_alive(false) 写的是 close", "[http][response]") {
    const Response res = Response{}.with_status(200).keep_alive(false);
    CHECK(res.to_head_string().find("Connection: close\r\n") !=
          std::string::npos);
}

TEST_CASE("with_file 按后缀推断 MIME 类型", "[http][response]") {
    CHECK(Response{}
              .with_file("/x/index.html")
              .header_params_.at("Content-Type") == "text/html; charset=utf-8");
    CHECK(Response{}.with_file("/x/a.css").header_params_.at("Content-Type") ==
          "text/css; charset=utf-8");
    CHECK(Response{}.with_file("/x/a.png").header_params_.at("Content-Type") ==
          "image/png");
    CHECK(Response{}.with_file("/x/a.bin").header_params_.at("Content-Type") ==
          "application/octet-stream");
}
