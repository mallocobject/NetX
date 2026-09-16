#pragma once

#include "netx/core/expected.hpp"
#include "netx/net/endian.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cerrno>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/uio.h>
#include <utility>
#include <vector>

namespace netx::net::details {

// 字节流读缓冲区：prepend / append / retrieve 三段式布局
//
//   [0, rptr_)          已读空间，留给 prepend 往回填协议头
//   [rptr_, wptr_)      可读数据
//   [wptr_, data_.size())  可写空间
class Buffer {
  public:
    inline static constexpr std::size_t kShrinkThreshold = 16 * 1024;
    inline static constexpr std::size_t kInitBufferSize = 1024;
    inline static constexpr std::size_t kPrependSize = 32;
    inline static constexpr std::array<std::byte, 2> kCRLF{std::byte{'\r'},
                                                           std::byte{'\n'}};

  private:
    std::vector<std::byte> data_;
    std::size_t rptr_{kPrependSize};
    std::size_t wptr_{kPrependSize};

  public:
    Buffer() : data_(kInitBufferSize) {
    }

    // 移动后源对象只保证可析构/可重新赋值（rptr_/wptr_ 归零），不应继续使用
    Buffer(Buffer &&other) noexcept
        : data_(std::move(other.data_)), rptr_(std::exchange(other.rptr_, 0)),
          wptr_(std::exchange(other.wptr_, 0)) {
    }

    Buffer &operator=(Buffer &&other) noexcept {
        if (this != &other) {
            data_ = std::move(other.data_);
            rptr_ = std::exchange(other.rptr_, 0);
            wptr_ = std::exchange(other.wptr_, 0);
        }
        return *this;
    }

    // 交换全部状态
    void swap(Buffer &other) noexcept {
        data_.swap(other.data_);
        std::swap(rptr_, other.rptr_);
        std::swap(wptr_, other.wptr_);
    }

    // ------------------------------ 容量查询 ------------------------------

    [[nodiscard]] std::size_t readable_bytes() const noexcept {
        assert(wptr_ >= rptr_);
        return wptr_ - rptr_;
    }

    [[nodiscard]] std::size_t writable_bytes() const noexcept {
        assert(data_.size() >= wptr_);
        return data_.size() - wptr_;
    }

    [[nodiscard]] std::size_t prependable_bytes() const noexcept {
        return rptr_;
    }

    // 三段之和 == 底层 vector 的容量
    [[nodiscard]] std::size_t capacity() const noexcept {
        return data_.size();
    }

    // ------------------------------ 区间视图 ------------------------------

    [[nodiscard]] std::span<const std::byte> readable() const noexcept {
        return {data_.data() + rptr_, readable_bytes()};
    }

    [[nodiscard]] std::span<std::byte> writable() noexcept {
        return {data_.data() + wptr_, writable_bytes()};
    }

    // 文本场景的便捷视图
    [[nodiscard]] const char *peek() const noexcept {
        return reinterpret_cast<const char *>(data_.data() + rptr_);
    }

    [[nodiscard]] std::string_view peek_string() const noexcept {
        return {peek(), readable_bytes()};
    }

    // -------------------------------- 取出 --------------------------------

    void retrieve(std::size_t len) noexcept {
        assert(len <= readable_bytes());
        if (len < readable_bytes()) {
            rptr_ += len;
        } else {
            retrieve_all();
        }
    }

    [[nodiscard]] std::string retrieve_string(std::size_t len) {
        assert(len <= readable_bytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    // -------------------------------- 写入 --------------------------------

    void append(std::span<const std::byte> data) {
        ensure_writable_bytes(data.size());
        std::ranges::copy(data,
                          data_.begin() + static_cast<std::ptrdiff_t>(wptr_));
        wptr_ += data.size();
    }

    void append(std::string_view data) {
        append(std::as_bytes(std::span{data}));
    }

    void append(const char *data, std::size_t len) {
        append(std::string_view{data, len});
    }

    // prepend 的可用空间只有 kPrependSize，超出是调用方的编排错误。
    // 这里显式抛异常而不是只 assert：release 下 rptr_ 会无符号下溢成
    // 巨大偏移，随后就是越界写。
    void prepend(std::span<const std::byte> data) {
        if (data.size() > prependable_bytes()) [[unlikely]] {
            throw std::out_of_range{"Buffer::prepend: 预留区不足"};
        }
        rptr_ -= data.size();
        std::ranges::copy(data,
                          data_.begin() + static_cast<std::ptrdiff_t>(rptr_));
    }

    void prepend(std::string_view data) {
        prepend(std::as_bytes(std::span{data}));
    }

    // 保证至少 len 字节可写（必要时扩容或把数据搬到头部）
    void ensure_writable_bytes(std::size_t len) {
        if (len > writable_bytes()) {
            extend_space(len);
        }
    }

    void shrink(std::size_t extra_size) {
        Buffer tmp;
        tmp.ensure_writable_bytes(readable_bytes() + extra_size);
        tmp.append(readable());
        swap(tmp);
    }

    // 供高频调用（例如每处理完一个请求就调一次）。两条保证：
    //
    //   1. 不需要缩时只做一次比较就返回 —— 不分配、不拷贝
    //   2. 缩完再调必然不再触发 —— 判据里已经含了缩完的目标尺寸，
    //      不会出现"每次调用都重新分配一遍"
    //
    // kShrinkThreshold 在这里是触发门槛，不是容量上限（append 想涨就涨）：
    // 日常的大小波动不值得去缩，只有缓冲区真的涨过这个量级才动手，
    // 以此避免"缩了又长"的来回抖动。
    void try_shrink() {
        const std::size_t target =
            kPrependSize + readable_bytes() + kInitBufferSize;
        if (data_.size() > std::max(target, kShrinkThreshold)) {
            shrink(kInitBufferSize); // 留出余量，避免下次 append 立刻又扩容
        }
    }

    // ------------------------------- 整数 --------------------------------
    // 统一走 std::bit_cast 而不是 memcpy：类型安全、无指针别名问题，
    // 且与 address.hpp 的字节序处理保持一致。

    template <std::integral T>
        requires(!std::same_as<T, bool>)
    void append_integer(T x) {
        append(std::bit_cast<std::array<std::byte, sizeof(T)>>(host2be(x)));
    }

    template <std::integral T>
        requires(!std::same_as<T, bool>)
    void prepend_integer(T x) {
        prepend(std::bit_cast<std::array<std::byte, sizeof(T)>>(host2be(x)));
    }

    // 数据不足时抛异常：原来只 assert，release 下 memcpy 会直接越界读
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    [[nodiscard]] T peek_integer() const {
        if (readable_bytes() < sizeof(T)) [[unlikely]] {
            throw std::out_of_range{"Buffer::peek_integer: 可读数据不足"};
        }
        std::array<std::byte, sizeof(T)> raw{};
        std::ranges::copy(readable().first(sizeof(T)), raw.begin());
        return be2host(std::bit_cast<T>(raw));
    }

    template <std::integral T>
        requires(!std::same_as<T, bool>)
    T retrieve_integer() {
        const T v = peek_integer<T>();
        retrieve(sizeof(T));
        return v;
    }

    // ------------------------------- 查找 --------------------------------

    // 返回 CRLF 在可读区间里的起始偏移，没找到返回 nullopt。
    // 返回偏移而非裸指针：extend_space 一旦扩容，之前拿到的指针就失效了，
    // 偏移不会。
    [[nodiscard]] std::optional<std::size_t>
    find_CRLF(std::size_t offset = 0) const noexcept {
        const auto view = readable();
        if (offset > view.size()) {
            return std::nullopt;
        }

        const auto hay = view.subspan(offset);
        const auto it = std::ranges::search(hay, kCRLF).begin();
        if (it == hay.end()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(it - hay.begin()) + offset;
    }

    // ------------------------------- 读取 --------------------------------

    // 从 fd 读入数据。返回值语义：
    //   > 0   读到了这么多字节，已并入可读区间
    //   0     非阻塞 fd 上暂时无数据（EAGAIN/EWOULDBLOCK），不是错误
    //   失败  真实错误；对端关闭也会以 BrokenPipe 形式返回
    //
    // 不用 -1 当"无数据"的哨兵：那是把信号塞进成功值通道，调用方只要按
    // "有值就是字节数"来用，就会把 -1 当成负数长度。
    [[nodiscard]] core::Expected<std::size_t> read_fd(int fd);

  private:
    void retrieve_all() noexcept {
        rptr_ = wptr_ = kPrependSize;
    }

    void extend_space(std::size_t len);
};

inline core::Expected<std::size_t> Buffer::read_fd(int fd) {
    // readv 的第二个 iovec 指向这里，用来一次性把内核缓冲读空
    thread_local std::array<std::byte, 65536> extra_buf{};

    const std::size_t room = writable_bytes();

    std::array<iovec, 2> vec{};
    vec[0].iov_base = data_.data() + wptr_;
    vec[0].iov_len = room;
    vec[1].iov_base = extra_buf.data();
    vec[1].iov_len = extra_buf.size();

    const ssize_t n = ::readv(fd, vec.data(), static_cast<int>(vec.size()));

    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return 0; // 暂时没数据，等下一轮可读事件
        }
        // 不在这里记日志：调用方拿到同一个 error_code，而且它知道是哪个
        // fd、哪个对端，记出来的东西信息更全
        return core::details::from_errno_to_unexpected(errno);
    }

    if (n == 0) {
        return std::unexpected{
            core::details::make_error_code(core::details::Error::BrokenPipe)};
    }

    const auto total = static_cast<std::size_t>(n);
    if (total <= room) {
        wptr_ += total;
    } else {
        // 主缓冲被写满，多出来的部分在 extra_buf 里
        wptr_ = data_.size();
        append(std::span<const std::byte>{extra_buf.data(), total - room});
    }

    return total;
}

inline void Buffer::extend_space(std::size_t len) {
    // 可写 + 已读预留区仍不够 len + kPrependSize 时只能扩容；
    // 否则把可读数据搬到 kPrependSize 处，复用前面已经读掉的空间
    if (writable_bytes() + prependable_bytes() < len + kPrependSize) {
        data_.resize(wptr_ + len);
    } else {
        const std::size_t readable = readable_bytes();
        std::ranges::copy(std::span{data_.data() + rptr_, readable},
                          data_.begin() +
                              static_cast<std::ptrdiff_t>(kPrependSize));
        rptr_ = kPrependSize;
        wptr_ = rptr_ + readable;
    }
}
} // namespace netx::net::details
