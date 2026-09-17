// netx/net/buffer.hpp 的测试。
//
// 除了常规往返，重点盯住几个容易写错的不变量：
//   1. peek_integer 数据不足时必须报错 —— 原来只有 assert，release 下 memcpy
//      会直接越界读
//   2. prepend 超出预留区必须报错 —— 原来只有 assert，release 下 rptr_ 无符号
//      下溢成巨大偏移，随后是越界写
//   3. read_fd 无数据时返回 0（成功值）而不是 -1 哨兵 —— -1 塞进成功值通道，
//      调用方按"有值就是字节数"用就会拿到负数长度
//   4. read_fd 走 extra_buf 的溢出路径不能丢数据

#include "netx/net/buffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <endian.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <utility>

using netx::core::details::Error;
using netx::core::details::make_error_code;
using netx::net::details::Buffer;

// 统计全局 operator new 的调用次数，用来证明 try_shrink 的 no-op 分支
// 一次都没分配。光看 capacity / peek 指针这类可观察状态不够 —— 在反复
// 缩放的场景下它们可能保持不变，但底层其实每次都重新分配了一遍。
namespace {
std::atomic<std::size_t> g_alloc_count{0};
}

// -Wmismatched-new-delete 在这里是误报：这一对是成套的（new 用 malloc、
// delete 用 free），只是 GCC 看不穿被替换掉的全局 operator new。
// 只在计数这几个函数上关掉，别的影响面太大。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"

void *operator new(std::size_t n) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(n)) {
        return p;
    }
    throw std::bad_alloc{};
}

void *operator new[](std::size_t n) {
    return ::operator new(n);
}

void operator delete(void *p) noexcept {
    std::free(p);
}

void operator delete[](void *p) noexcept {
    std::free(p);
}

void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}

void operator delete[](void *p, std::size_t) noexcept {
    std::free(p);
}

#pragma GCC diagnostic pop

namespace {

// 非阻塞管道：read_fd 的测试载体
struct Pipe {
    int rfd{-1};
    int wfd{-1};

    Pipe() {
        int fds[2] = {-1, -1};
        if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0) {
            rfd = fds[0];
            wfd = fds[1];
        }
    }

    ~Pipe() {
        if (rfd != -1) {
            ::close(rfd);
        }
        if (wfd != -1) {
            ::close(wfd);
        }
    }

    Pipe(const Pipe &) = delete;
    Pipe &operator=(const Pipe &) = delete;

    [[nodiscard]] bool valid() const noexcept {
        return rfd != -1;
    }

    void close_write() {
        if (wfd != -1) {
            ::close(wfd);
            wfd = -1;
        }
    }
};

// 三段之和就是底层 vector 的容量
std::size_t capacity_of(const Buffer &b) {
    return b.readable_bytes() + b.writable_bytes() + b.prependable_bytes();
}

std::size_t alloc_count() {
    return g_alloc_count.load(std::memory_order_relaxed);
}

} // namespace

TEST_CASE("新建缓冲区只有可写空间", "[net][buffer]") {
    const Buffer b;
    CHECK(b.readable_bytes() == 0);
    CHECK(b.prependable_bytes() == Buffer::kPrependSize);
    CHECK(capacity_of(b) == Buffer::kInitBufferSize);
    CHECK(b.peek_string().empty());
    CHECK_FALSE(b.find_CRLF().has_value());
}

TEST_CASE("append 与 retrieve 往返", "[net][buffer]") {
    Buffer b;
    b.append("hello ");
    b.append(std::string_view{"world"});
    CHECK(b.readable_bytes() == 11);
    CHECK(b.peek_string() == "hello world");

    b.retrieve(6);
    CHECK(b.peek_string() == "world");
    CHECK(b.readable_bytes() == 5);

    b.retrieve(5); // 读干净后回到初始状态
    CHECK(b.readable_bytes() == 0);
    CHECK(b.prependable_bytes() == Buffer::kPrependSize);
}

TEST_CASE("retrieve_string 取出并消费", "[net][buffer]") {
    Buffer b;
    b.append("abcdef");

    CHECK(b.retrieve_string(3) == "abc");
    CHECK(b.readable_bytes() == 3);
    CHECK(b.peek_string() == "def");
}

TEST_CASE("append 超出容量时扩容且内容不丢", "[net][buffer]") {
    Buffer b;
    const std::string big(Buffer::kInitBufferSize * 3, 'x');

    b.append(big);
    CHECK(capacity_of(b) >= big.size());
    CHECK(b.readable_bytes() == big.size());
    CHECK(b.peek_string() == big);
}

TEST_CASE("retrieve 之后 append 复用已读空间而不扩容", "[net][buffer]") {
    Buffer b;
    b.append(std::string(b.writable_bytes(), 'A'));
    const std::size_t full_capacity = capacity_of(b);
    REQUIRE(b.writable_bytes() == 0);

    // 读掉大部分，让前面腾出足够空间走"搬移"分支
    b.retrieve(900);
    const std::size_t readable_before = b.readable_bytes();
    REQUIRE(b.prependable_bytes() == 900 + Buffer::kPrependSize);

    b.append(std::string(500, 'B'));

    // 容量不变 == 没有重新分配；prependable 回到 kPrependSize == 数据被搬到了
    // 头部。这两条合起来唯一确定了走的是"搬移"分支，不需要额外的计数器。
    CHECK(b.capacity() == full_capacity);
    CHECK(b.prependable_bytes() == Buffer::kPrependSize);
    CHECK(b.readable_bytes() == readable_before + 500);
    CHECK(b.peek_string().substr(readable_before) == std::string(500, 'B'));
}

TEST_CASE("只有装不下时容量才增长", "[net][buffer]") {
    Buffer b;
    b.append(std::string(b.writable_bytes(), 'A'));
    const std::size_t full_capacity = b.capacity();

    b.retrieve(900);
    b.append(std::string(500, 'B')); // 复用已读空间，容量不动
    CHECK(b.capacity() == full_capacity);
    CHECK(b.prependable_bytes() == Buffer::kPrependSize);

    b.append(std::string(Buffer::kInitBufferSize, 'C')); // 这次真的装不下
    CHECK(b.capacity() > full_capacity);
    CHECK(b.readable_bytes() == 92 + 500 + Buffer::kInitBufferSize);
}

TEST_CASE("capacity 与三段之和一致", "[net][buffer]") {
    Buffer b;
    CHECK(b.capacity() == Buffer::kInitBufferSize);
    CHECK(b.capacity() ==
          b.readable_bytes() + b.writable_bytes() + b.prependable_bytes());

    b.append(std::string(Buffer::kInitBufferSize * 2, 'x'));
    CHECK(b.capacity() ==
          b.readable_bytes() + b.writable_bytes() + b.prependable_bytes());
    CHECK(b.capacity() >= Buffer::kInitBufferSize * 2);
}

TEST_CASE("prepend 在预留区内工作", "[net][buffer]") {
    Buffer b;
    b.append("body");
    b.prepend(std::string_view{"head:"});

    CHECK(b.readable_bytes() == 9);
    CHECK(b.peek_string() == "head:body");
    CHECK(b.prependable_bytes() == Buffer::kPrependSize - 5);
}

TEST_CASE("prepend 超出预留区抛 out_of_range", "[net][buffer]") {
    Buffer b;
    // 预留区就是 kPrependSize，多一个字节都放不下
    CHECK_NOTHROW(b.prepend(std::string(Buffer::kPrependSize, 'p')));
    CHECK_THROWS_AS(b.prepend(std::string_view{"x"}), std::out_of_range);
}

TEST_CASE("整数以网络字节序写入", "[net][buffer]") {
    Buffer b;
    b.append_integer<std::uint32_t>(0x11223344);
    b.append_integer<std::uint16_t>(0xABCD);

    // 直接和 glibc 的 htobe* 对齐，确认落盘的是大端
    const std::uint32_t be32 = htobe32(0x11223344);
    const std::uint16_t be16 = htobe16(0xABCD);
    REQUIRE(b.readable_bytes() == 6);
    CHECK(std::memcmp(b.peek(), &be32, sizeof(be32)) == 0);
    CHECK(std::memcmp(b.peek() + 4, &be16, sizeof(be16)) == 0);

    // 读回来是主机序
    CHECK(b.retrieve_integer<std::uint32_t>() == 0x11223344);
    CHECK(b.retrieve_integer<std::uint16_t>() == 0xABCD);
    CHECK(b.readable_bytes() == 0);
}

TEST_CASE("整数覆盖各宽度与有符号类型", "[net][buffer]") {
    Buffer b;
    b.append_integer<std::uint8_t>(0x7F);
    b.append_integer<std::int16_t>(-2);
    b.append_integer<std::int32_t>(-123456);
    b.append_integer<std::uint64_t>(0xDEADBEEFCAFEBABEull);

    CHECK(b.retrieve_integer<std::uint8_t>() == 0x7F);
    CHECK(b.retrieve_integer<std::int16_t>() == -2);
    CHECK(b.retrieve_integer<std::int32_t>() == -123456);
    CHECK(b.retrieve_integer<std::uint64_t>() == 0xDEADBEEFCAFEBABEull);
}

TEST_CASE("peek_integer 数据不足时抛 out_of_range", "[net][buffer]") {
    Buffer b;
    b.append_integer<std::uint16_t>(7);

    CHECK(b.peek_integer<std::uint16_t>() == 7);
    // 恰好等于 sizeof(T) 是合法的，不该被当成不足
    CHECK_NOTHROW(b.peek_integer<std::uint16_t>());
    // 少一个字节就必须报错，而不是越界读
    CHECK_THROWS_AS(b.peek_integer<std::uint32_t>(), std::out_of_range);

    Buffer empty;
    CHECK_THROWS_AS(empty.peek_integer<std::uint8_t>(), std::out_of_range);
}

TEST_CASE("find_CRLF 返回偏移并支持续搜", "[net][buffer]") {
    Buffer b;
    b.append("hello\r\n\r\nworld");

    const auto first = b.find_CRLF();
    REQUIRE(first.has_value());
    CHECK(*first == 5);

    const auto second = b.find_CRLF(*first + 2);
    REQUIRE(second.has_value());
    CHECK(*second == 7);

    CHECK_FALSE(b.find_CRLF(*second + 2).has_value());
    CHECK_FALSE(b.find_CRLF(9999).has_value()); // 偏移越界
}

TEST_CASE("find_CRLF 只有半个 CRLF 时不算命中", "[net][buffer]") {
    Buffer b;
    b.append("abc\r");

    CHECK_FALSE(b.find_CRLF().has_value());
    CHECK(b.readable_bytes() == 4);
}

TEST_CASE("read_fd 把数据读进可读区间", "[net][buffer]") {
    Pipe p;
    REQUIRE(p.valid());

    const std::string msg = "hello netx";
    REQUIRE(::write(p.wfd, msg.data(), msg.size()) ==
            static_cast<ssize_t>(msg.size()));

    Buffer b;
    const auto n = b.read_fd(p.rfd);
    REQUIRE(n.has_value());
    CHECK(*n == msg.size());
    CHECK(b.readable_bytes() == msg.size());
    CHECK(b.peek_string() == msg);
}

TEST_CASE("read_fd 无数据时返回 0 而不是错误", "[net][buffer]") {
    Pipe p;
    REQUIRE(p.valid());

    Buffer b;
    const auto n = b.read_fd(p.rfd);

    // 关键：EAGAIN 不是错误，走成功通道；值是 0 而不是 -1
    REQUIRE(n.has_value());
    CHECK(*n == 0);
    CHECK(b.readable_bytes() == 0);
}

TEST_CASE("read_fd 对端关闭时报 BrokenPipe", "[net][buffer]") {
    Pipe p;
    REQUIRE(p.valid());
    p.close_write();

    Buffer b;
    const auto n = b.read_fd(p.rfd);
    REQUIRE_FALSE(n.has_value());
    CHECK(n.error() == make_error_code(Error::BrokenPipe));
}

TEST_CASE("read_fd 主缓冲写满时经 extra_buf 落盘不丢数据", "[net][buffer]") {
    Pipe p;
    REQUIRE(p.valid());

    Buffer b;
    // 先把主缓冲填满，让 writable_bytes() == 0，逼 readv 只用第二个 iovec
    const std::size_t fill = b.writable_bytes();
    b.append(std::string(fill, 'F'));
    REQUIRE(b.writable_bytes() == 0);

    const std::string tail = "overflow-tail";
    REQUIRE(::write(p.wfd, tail.data(), tail.size()) ==
            static_cast<ssize_t>(tail.size()));

    const auto n = b.read_fd(p.rfd);
    REQUIRE(n.has_value());
    CHECK(*n == tail.size());
    CHECK(b.readable_bytes() == fill + tail.size());
    CHECK(b.peek_string().substr(fill) == tail);
}

TEST_CASE("read_fd 在数据超过主缓冲剩余空间时跨两段", "[net][buffer]") {
    Pipe p;
    REQUIRE(p.valid());

    Buffer b;
    // 只留一小截可写空间，写入远超它的数据
    const std::size_t small = 8;
    b.append(std::string(b.writable_bytes() - small, 'A'));

    const std::string tail(1000, 'B');
    REQUIRE(::write(p.wfd, tail.data(), tail.size()) ==
            static_cast<ssize_t>(tail.size()));

    const auto n = b.read_fd(p.rfd);
    REQUIRE(n.has_value());
    CHECK(*n == tail.size());
    CHECK(b.peek_string().substr(b.readable_bytes() - tail.size()) == tail);
}

TEST_CASE("shrink 压缩到刚好够用", "[net][buffer]") {
    Buffer b;
    b.append(std::string(Buffer::kInitBufferSize * 2, 'x'));
    b.retrieve(Buffer::kInitBufferSize * 2 - 10); // 只剩 10 字节

    b.shrink(0);
    CHECK(b.readable_bytes() == 10);
    CHECK(b.prependable_bytes() == Buffer::kPrependSize);
    CHECK(b.peek_string() == std::string(10, 'x'));
    CHECK(capacity_of(b) < Buffer::kInitBufferSize * 2);
}

TEST_CASE("try_shrink 带可读数据也能瘦身并保留余量", "[net][buffer]") {
    Buffer b;
    b.append(std::string(50 * 1024, 'z'));
    b.retrieve(40 * 1024);

    const std::string kept(10 * 1024, 'z');
    REQUIRE(b.readable_bytes() == kept.size());
    REQUIRE(b.capacity() > Buffer::kShrinkThreshold);

    b.try_shrink();

    CHECK(b.capacity() < 50 * 1024);                      // 真的缩了
    CHECK(b.peek_string() == kept);                       // 数据完整
    CHECK(b.writable_bytes() >= Buffer::kInitBufferSize); // 留了余量
}

TEST_CASE("容量没超门槛时 try_shrink 不动手", "[net][buffer]") {
    Buffer b;
    b.append(std::string(Buffer::kShrinkThreshold / 2, 'y'));
    const std::size_t small = capacity_of(b);
    REQUIRE(small <= Buffer::kShrinkThreshold);

    b.retrieve(b.readable_bytes());
    for (int i = 0; i < 100; ++i) {
        b.try_shrink();
    }
    CHECK(capacity_of(b) == small);
}

TEST_CASE("try_shrink 可高频调用：不需要缩时零分配", "[net][buffer]") {
    Buffer b;
    // 可读数据本身就超过门槛 —— 这种情形最容易让"缩完再调"反复触发
    b.append(std::string(100 * 1024, 'x'));
    b.retrieve(50 * 1024);
    REQUIRE(b.readable_bytes() > Buffer::kShrinkThreshold);

    b.try_shrink(); // 第一次：真的缩
    const std::size_t settled = b.capacity();
    const std::string kept(50 * 1024, 'x');
    REQUIRE(b.peek_string() == kept);

    // 之后每次调用都必须是一次比较就返回。直接数分配次数：判据里已经含了
    // 缩完的目标尺寸，所以不该再有 shrink()，也就不该再有 new。
    const std::size_t before = alloc_count();
    for (int i = 0; i < 1000; ++i) {
        b.try_shrink();
    }
    CHECK(alloc_count() == before);
    CHECK(b.capacity() == settled);
    CHECK(b.peek_string() == kept);
}

TEST_CASE("移动构造与移动赋值", "[net][buffer]") {
    Buffer src;
    src.append("payload");

    Buffer moved{std::move(src)};
    CHECK(moved.peek_string() == "payload");
    CHECK(moved.readable_bytes() == 7);

    Buffer assigned;
    assigned = std::move(moved);
    CHECK(assigned.peek_string() == "payload");

    // 移动赋值给自身不应破坏内容
    Buffer &self = assigned;
    assigned = std::move(self);
    CHECK(assigned.peek_string() == "payload");
}

TEST_CASE("swap 交换两边的全部状态", "[net][buffer]") {
    Buffer a;
    Buffer b;
    a.append("aaa");

    const std::string big(Buffer::kInitBufferSize * 2, 'b');
    b.append(big); // 必然扩容

    const std::size_t a_capacity = a.capacity();
    const std::size_t b_capacity = b.capacity();
    REQUIRE(a_capacity != b_capacity);

    a.swap(b);

    CHECK(a.peek_string() == big);
    CHECK(b.peek_string() == "aaa");
    // 容量也是状态的一部分，必须跟着换过去
    CHECK(a.capacity() == b_capacity);
    CHECK(b.capacity() == a_capacity);
}
