// netx/http/sender.hpp 的测试。
//
// 普通响应与文件响应走的是两条完全不同的路：前者 head+body 一次 writev，
// 后者 mmap 之后分片发。文件这条路此前没有任何覆盖，而它涉及 open / fstat /
// mmap / 缓存 / 404 回退好几条分支。

#include "netx/http/sender.hpp"
#include "netx/core/async_main.hpp"
#include "netx/http/response.hpp"
#include "netx/net/stream.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using netx::core::async_main;
using netx::http::Response;
using netx::http::details::Sender;
using netx::net::details::Stream;

namespace {

/// 一对非阻塞 socket；a 交给 Stream，b 留给测试读线上字节
struct Pair {
    int a{-1};
    int b{-1};

    Pair() {
        int fds[2] = {-1, -1};
        if (::socketpair(
                AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) ==
            0) {
            a = fds[0];
            b = fds[1];
        }
    }

    ~Pair() {
        if (a != -1) {
            ::close(a);
        }
        if (b != -1) {
            ::close(b);
        }
    }

    Pair(const Pair &) = delete;
    Pair &operator=(const Pair &) = delete;

    void release_a() {
        a = -1;
    }
};

/// 读到至少 want 字节，或者对端不再有数据为止。
/// 两端都是 SOCK_NONBLOCK，read 会返回 EAGAIN 而不是阻塞 —— 不等 poll
/// 直接 break 就会提前收工，把后面的用例坑成"数据没到齐"。
std::string read_all(int fd, size_t want) {
    std::string out;
    while (out.size() < want) {
        pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
        if (::poll(&pfd, 1, 300) <= 0) {
            break;
        }
        std::array<char, 65536> buf{};
        const ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n > 0) {
            out.append(buf.data(), static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        }
    }
    return out;
}

/// 每个用例用自己的临时目录，避免 Sender 里那个按路径缓存的文件表串味
struct TempDir {
    std::filesystem::path dir;

    TempDir() {
        static int counter = 0;
        dir = std::filesystem::temp_directory_path() /
              ("netx_sender_" + std::to_string(::getpid()) + "_" +
               std::to_string(counter++));
        std::filesystem::create_directories(dir);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    std::string write_file(const std::string &name,
                           const std::string &content) const {
        const auto path = dir / name;
        std::ofstream out{path, std::ios::binary};
        out << content;
        out.close();
        return path.string();
    }

    std::string path_of(const std::string &name) const {
        return (dir / name).string();
    }
};

/// 把 Response 发出去，返回线上收到的全部字节
std::string send_and_collect(Response &res, size_t want = 8192) {
    Pair pair;
    REQUIRE(pair.a != -1);

    auto stream = Stream::create(pair.a, ::dup(pair.a));
    REQUIRE(stream.has_value());
    pair.release_a();

    // 必须边写边排空：文件用例会写 300KB，早就超过 socket 缓冲，没有读者
    // 的话写端会卡在等可写上，async_main 永远不返回。
    std::string collected;
    std::thread reader{[&] { collected = read_all(pair.b, want); }};

    const auto sent = async_main(Sender::send(*stream, res));
    reader.join();

    REQUIRE(sent.has_value());
    return collected;
}

} // namespace

TEST_CASE("普通响应 head 与 body 一起发出来", "[http][sender]") {
    Response res = Response{}
                       .with_status(200)
                       .content_type("text/plain")
                       .with_body("hello body");

    const std::string wire = send_and_collect(res, 128);

    CHECK(wire.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(wire.find("Content-Length: 10\r\n") != std::string::npos);
    CHECK(wire.ends_with("\r\n\r\nhello body"));
}

TEST_CASE("文件响应按 mmap 发出，Content-Length 等于文件大小",
          "[http][sender]") {
    TempDir tmp;
    const std::string content = "file content here";
    Response res = Response{}.with_file(tmp.write_file("a.txt", content));

    const std::string wire = send_and_collect(res, 4096);

    CHECK(wire.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(wire.find("Content-Length: " + std::to_string(content.size()) +
                    "\r\n") != std::string::npos);
    CHECK(wire.find("Content-Type: text/plain; charset=utf-8\r\n") !=
          std::string::npos);
    CHECK(wire.ends_with("\r\n\r\n" + content));
}

TEST_CASE("超过单个分片的大文件完整发出", "[http][sender]") {
    TempDir tmp;

    // kSendChunk 是 128KB，这里给 300KB，必然走多片那条循环
    std::string content;
    content.reserve(300 * 1024);
    for (size_t i = 0; i < 300 * 1024; ++i) {
        content.push_back(static_cast<char>('a' + (i % 26)));
    }
    Response res = Response{}.with_file(tmp.write_file("big.bin", content));

    const std::string wire = send_and_collect(res, content.size() + 256);

    const size_t head_end = wire.find("\r\n\r\n");
    REQUIRE(head_end != std::string::npos);
    CHECK(wire.find("Content-Length: " + std::to_string(content.size()) +
                    "\r\n") != std::string::npos);

    const std::string body = wire.substr(head_end + 4);
    CHECK(body.size() == content.size());
    CHECK(body == content); // 分片边界不能错位、不能丢字节
}

TEST_CASE("空文件回 200 和 Content-Length: 0", "[http][sender]") {
    TempDir tmp;
    // mmap 长度为 0 会 EINVAL，这条分支必须单独回一个空 body
    Response res = Response{}.with_file(tmp.write_file("empty.txt", ""));

    const std::string wire = send_and_collect(res, 256);

    CHECK(wire.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(wire.find("Content-Length: 0\r\n") != std::string::npos);
    CHECK(wire.ends_with("\r\n\r\n"));
}

TEST_CASE("文件不存在回 404 并带说明 body", "[http][sender]") {
    TempDir tmp;
    Response res = Response{}.with_file(tmp.path_of("nope.txt"));

    const std::string wire = send_and_collect(res, 256);

    CHECK(wire.starts_with("HTTP/1.1 404 Not Found\r\n"));
    CHECK(wire.ends_with("<h1>404 Not Found</h1>"));
}

TEST_CASE("目录也回 404（不是普通文件）", "[http][sender]") {
    TempDir tmp;
    Response res = Response{}.with_file(tmp.dir.string());

    const std::string wire = send_and_collect(res, 256);

    CHECK(wire.starts_with("HTTP/1.1 404 Not Found\r\n"));
}

TEST_CASE("同一个文件第二次发送走缓存：文件被换掉也仍返回首次映射那份",
          "[http][sender]") {
    // 这条用例**在钉住当前语义**：缓存没有失效机制，所以文件被替换之后
    // 服务端还会一直返回第一次映射进来的那份内容。将来若加了 mtime 校验，
    // 该改的是这条用例，而不是把缓存改回去。
    //
    // 必须用 rename 换掉 inode：原地 truncate + 重写会命中同一批页，那样
    // mmap 直接就能看到新内容，根本区分不出"缓存命中"和"重新映射"。
    TempDir tmp;
    const std::string first = "first version";
    const std::string path = tmp.write_file("cache.txt", first);

    Response res1 = Response{}.with_file(path);
    const std::string wire1 = send_and_collect(res1, 256);
    CHECK(wire1.ends_with("\r\n\r\n" + first));

    const std::string other =
        tmp.write_file("other.txt", "completely different content");
    std::filesystem::rename(other, path);

    Response res2 = Response{}.with_file(path);
    const std::string wire2 = send_and_collect(res2, 256);
    CHECK(wire2.ends_with("\r\n\r\n" + first)); // 仍是老 inode 的内容
    CHECK(wire2.find("Content-Length: " + std::to_string(first.size()) +
                     "\r\n") != std::string::npos);
}

TEST_CASE("两个不同的文件各自正确", "[http][sender]") {
    TempDir tmp;
    const std::string a = "AAAA";
    const std::string b = "BBBBBBBB";

    const std::string path_a = tmp.write_file("a.dat", a);
    const std::string path_b = tmp.write_file("b.dat", b);

    Response res_a = Response{}.with_file(path_a);
    const std::string wire_a = send_and_collect(res_a, 256);
    CHECK(wire_a.ends_with("\r\n\r\n" + a));

    Response res_b = Response{}.with_file(path_b);
    const std::string wire_b = send_and_collect(res_b, 256);
    CHECK(wire_b.ends_with("\r\n\r\n" + b));
}
