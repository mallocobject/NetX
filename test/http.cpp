// netx/http 的纯逻辑部分测试。
//
// 挑的是不依赖 socket/事件循环、能真正断言行为的三块：
//   parser.hpp     —— HTTP 请求解析（状态机，最该被测）
//   radix_tree.hpp —— 路由匹配与路径归一化（安全相关）
//   response.hpp   —— 响应头拼装

#include "netx/http/field_map.hpp"
#include "netx/http/parser.hpp"
#include "netx/http/radix_tree.hpp"
#include "netx/http/response.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using netx::http::Response;
using netx::http::details::FieldMap;
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

TEST_CASE("feed 在报文边界停下，剩下的留给下一个请求", "[http][parser]") {
    Parser p;
    const std::string two = "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n";

    const auto first = p.feed(two);
    REQUIRE(first.has_value());
    CHECK(*first == two.find("GET /b")); // 一个字节都不多吃
    CHECK(p.completed());
    CHECK(p.req.url_path == "/a");

    // 剩下的接着喂
    p.clear();
    const auto second = p.feed(std::string_view{two}.substr(*first));
    REQUIRE(second.has_value());
    CHECK(p.completed());
    CHECK(p.req.url_path == "/b");
}

TEST_CASE("feed 对不完整输入返回已消费长度而不是报错", "[http][parser]") {
    Parser p;
    const std::string partial = "GET /abc HTTP/1.1\r\nHost: x";

    const auto n = p.feed(partial);
    REQUIRE(n.has_value());
    CHECK(*n == partial.size()); // 残缺的字段先攒着，等后续数据
    CHECK_FALSE(p.completed());
    CHECK(p.req.method == "GET");
    CHECK(p.req.url_path == "/abc");
    // header 要等本行的 '\r' 出现才入库，此刻它还在 tmp_value_ 里攒着
    CHECK(p.req.header("host").empty());

    // 补齐剩下的字节就能收尾
    REQUIRE(p.feed("\r\n\r\n").has_value());
    CHECK(p.completed());
    CHECK(p.req.header("host") == "x");
}

TEST_CASE("Content-Length 必须整个都是数字", "[http][parser]") {
    // stoul 会接受 "5abc" 的前缀，from_chars 不会 —— 这类值必须判非法
    Parser a;
    CHECK_FALSE(a.parse("POST / HTTP/1.1\r\nContent-Length: 5abc\r\n\r\n"));

    Parser b;
    CHECK_FALSE(b.parse("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n"));

    Parser c;
    CHECK_FALSE(c.parse("POST / HTTP/1.1\r\nContent-Length: \r\n\r\n"));

    Parser d;
    CHECK(d.parse("POST / HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc"));
    CHECK(d.req.body == "abc");
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
    REQUIRE(tree.insert("/a/b", 1));
    REQUIRE(tree.insert("/a/c", 2));

    CHECK(tree.search("/a/b").value != nullptr);
    CHECK(*tree.search("/a/b").value == 1);
    CHECK(*tree.search("/a/c").value == 2);
    CHECK(tree.search("/a/d").value == nullptr);
    CHECK(tree.search("/a").value == nullptr); // 父节点不是叶子
}

TEST_CASE("根路由 /", "[http][radix]") {
    RadixTree<int> tree;
    REQUIRE(tree.insert("/", 7));
    REQUIRE(tree.search("/").value != nullptr);
    CHECK(*tree.search("/").value == 7);
}

TEST_CASE("参数路由 :id 能捕获段并放进 params", "[http][radix]") {
    RadixTree<int> tree;
    REQUIRE(tree.insert("/users/:id", 1));

    auto m = tree.search("/users/42");
    REQUIRE(m.value != nullptr);
    CHECK(*m.value == 1);
    CHECK(m.params.at("id") == "42");
}

TEST_CASE("静态段优先于参数段", "[http][radix]") {
    RadixTree<int> tree;
    REQUIRE(tree.insert("/users/:id", 1));
    REQUIRE(tree.insert("/users/me", 2));

    CHECK(*tree.search("/users/me").value == 2); // 静态优先
    CHECK(*tree.search("/users/99").value == 1); // 落到参数
}

TEST_CASE("同一位置不同名的参数段判为冲突", "[http][radix]") {
    RadixTree<int> tree;
    REQUIRE(tree.insert("/users/:id", 1));

    // 一个节点只能有一个参数名。原实现把名字存在父节点上，第二次注册会
    // 直接覆盖，于是 /users/:id 取到的键名变成了 "name"。
    CHECK_FALSE(tree.insert("/users/:name", 2));

    // 冲突不影响已经注册好的那条
    auto m = tree.search("/users/42");
    REQUIRE(m.value != nullptr);
    CHECK(m.params.at("id") == "42");
    CHECK(m.params.count("name") == 0);
}

TEST_CASE("同名参数段可以重复注册（后注册的覆盖处理函数）", "[http][radix]") {
    RadixTree<int> tree;
    REQUIRE(tree.insert("/u/:id", 1));
    REQUIRE(tree.insert("/u/:id", 2));

    auto m = tree.search("/u/7");
    REQUIRE(m.value != nullptr);
    CHECK(*m.value == 2);
    CHECK(m.params.at("id") == "7");
}

TEST_CASE("通配符 * 吃掉剩余所有段", "[http][radix]") {
    RadixTree<int> tree;
    REQUIRE(tree.insert("/static/*", 1));

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

TEST_CASE("normalize_in_place 就地做，结果与 normalize_path 一致",
          "[http][radix]") {
    // 就地版本只删不增，不需要额外分配；两条路径必须给出相同结果
    for (const char *raw : {"/a/./b",
                            "/a/b/../c",
                            "/a//b",
                            "/a/b/",
                            "",
                            "/",
                            "/../../etc/passwd",
                            "/a/../../../x",
                            "/..",
                            "/x/y/./z/../w"}) {
        std::string in_place = raw;
        RadixTree<int>::normalize_in_place(in_place);
        CHECK(in_place == RadixTree<int>::normalize_path(raw));
    }

    // 不以 '/' 开头时补一个前导斜杠
    std::string rel = "a/b";
    RadixTree<int>::normalize_in_place(rel);
    CHECK(rel == "/a/b");
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

// --------------------------------------------------------------- FieldMap

TEST_CASE("FieldMap 保留插入顺序并且覆盖不换位置", "[http][field_map]") {
    FieldMap m;
    CHECK(m.empty());

    m["b"] = "2";
    m["a"] = "1";
    CHECK(m.size() == 2);
    CHECK(m.get("a") == "1");
    CHECK(m.get("nope").empty()); // 不存在给空串
    CHECK(m.at("b") == "2");
    CHECK_THROWS_AS(m.at("nope"), std::out_of_range);
    CHECK(m.contains("a"));
    CHECK(m.count("a") == 1);
    CHECK(m.count("zzz") == 0);

    std::string order;
    for (const auto &[k, v] : m) {
        order += k;
    }
    CHECK(order == "ba"); // 插入顺序，不是字典序

    m["b"] = "9"; // 覆盖
    order.clear();
    for (const auto &[k, v] : m) {
        order += k;
    }
    CHECK(order == "ba");
    CHECK(m.size() == 2);
    CHECK(m.get("b") == "9");

    m.clear();
    CHECK(m.empty());
}

TEST_CASE("单个字段表能存多个同名键", "[http][field_map]") {
    // 同名 header 是合法的，unordered_map 存不下
    FieldMap m;
    m["set-cookie"] = "a=1";
    m["x-other"] = "z";
    CHECK(m.size() == 2);
    CHECK(m.get("set-cookie") == "a=1");
}

// -------------------------------------------------------------- Response

TEST_CASE("响应头按插入顺序输出", "[http][response]") {
    // 原实现用 unordered_map，遍历顺序是实现定义的 —— 每次跑都可能不一样，
    // 既没法断言也没法复现客户端的解析问题
    const Response res = Response{}
                             .with_status(200)
                             .with_header("Z-Last", "1")
                             .with_header("A-First", "2")
                             .with_header("M-Mid", "3");

    const std::string head = res.to_head_string();
    const size_t z = head.find("Z-Last");
    const size_t a = head.find("A-First");
    const size_t m = head.find("M-Mid");
    REQUIRE(z != std::string::npos);
    REQUIRE(a != std::string::npos);
    REQUIRE(m != std::string::npos);
    CHECK(z < a);
    CHECK(a < m);
}

TEST_CASE("with_header 覆盖同名而不是追加", "[http][response]") {
    const Response res = Response{}
                             .with_status(200)
                             .with_header("X-A", "1")
                             .with_header("X-A", "2");
    CHECK(res.header_params_.size() == 1);
    CHECK(res.header_params_.at("X-A") == "2");
}

TEST_CASE("扩展名只取最后一段，带点的目录名不误判", "[http][response]") {
    CHECK(Response{}
              .with_file("/x/index.html")
              .header_params_.at("Content-Type") == "text/html; charset=utf-8");
    CHECK(Response{}.with_file("/x/a.json").header_params_.at("Content-Type") ==
          "application/json; charset=utf-8");
    CHECK(Response{}.with_file("/x/a.webp").header_params_.at("Content-Type") ==
          "image/webp");
    // 目录名里带 .html，但文件本身没有扩展名
    CHECK(Response{}
              .with_file("/a.html/file")
              .header_params_.at("Content-Type") == "application/octet-stream");
    CHECK(Response{}.with_file("/x/noext").header_params_.at("Content-Type") ==
          "application/octet-stream");
}

TEST_CASE("with_body 接受字面量、string 与 string_view", "[http][response]") {
    CHECK(Response{}.with_body("lit").body == "lit");

    const std::string owned = "owned";
    CHECK(Response{}.with_body(owned).body == "owned");
    CHECK(Response{}.with_body(std::string{"moved"}).body == "moved");

    const std::string_view view = "view";
    CHECK(Response{}.with_body(view).body == "view");

    // Content-Length 跟着 body 走
    CHECK(Response{}.with_body("hello").header_params_.at("Content-Length") ==
          "5");
}
