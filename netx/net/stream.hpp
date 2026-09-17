#pragma once

#include "netx/core/event.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/net/address.hpp"
#include "netx/net/buffer.hpp"
#include "netx/net/socket.hpp"
#include <array>
#include <cerrno>
#include <cstddef>
#include <span>
#include <string_view>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <utility>

namespace netx::net::details {
// 一条双向流：读端和写端各持一个 fd，各自带一个可复用的就绪 awaiter。
//
// create(fd) 会把 fd 复制一份当写端（::dup），于是读端和写端是两个 fd、
// 同一个 socket；create(fd1, fd2) 则用调用方给的两个 fd，此时两者可能相同，
// close()/shutdown() 必须避免对同一个 fd 操作两次。
class Stream {
  private:
    // 这两个必须声明在最前面：C++ 按声明顺序初始化成员，而下面的构造
    // 函数要用 fd 参数喂给它们。若把它们放到 read_fd/write_fd 之后，初始化
    // 时读到的就是尚未初始化的 fd。
    core::details::EventLoop::EventAwaiter read_awaiter_;
    core::details::EventLoop::EventAwaiter write_awaiter_;

  public:
    int read_fd{-1};
    int write_fd{-1};

    Address sock_addr{};

    Buffer read_buf{};
    Buffer write_buf{};

  private:
    /// 把 write_buf 里的积压全部写出，必要时等可写
    core::Task<core::Expected<>> flush();

    explicit Stream(int fd1, int fd2)
        : read_awaiter_(core::details::EventLoop::loop().wait_event(
              {.fd = fd1, .flags = core::details::Event::kEventRead})),
          write_awaiter_(core::details::EventLoop::loop().wait_event(
              {.fd = fd2, .flags = core::details::Event::kEventWrite})),
          read_fd(fd1), write_fd(fd2) {
    }

  public:
    /// 接管 fd：置为非阻塞，并 ::dup 一份作为写端（两者是同一个 socket）
    static core::Expected<Stream> create(int fd) {
        if (auto exp = Socket::set_non_blocking(fd); !exp) {
            return std::unexpected{exp.error()};
        }

        const int w_fd = ::dup(fd);
        if (w_fd == -1) {
            return core::details::from_errno_to_unexpected(errno);
        }

        return Stream{fd, w_fd};
    }

    /// 用给定的两个 fd 建流。注意 fd1 与 fd2 可能是同一个 fd。
    static core::Expected<Stream> create(int fd1, int fd2) {
        // 两步都是 Expected<>、错误处理完全一致：串成一条链，短路由结构
        // 保证，错误路径只写一次；最后一步只是把两个 fd 组装成 Stream，
        // 不会再失败，用 transform 收尾
        return Socket::set_non_blocking(fd1)
            .and_then([fd2] { return Socket::set_non_blocking(fd2); })
            .transform([fd1, fd2] { return Stream{fd1, fd2}; });
    }

    /// 注销两个 awaiter 并关闭两边 fd。可重复调用；fd 0 也是合法的，
    /// 所以判据是 >= 0 而不是 > 0。
    core::Expected<> close() {
        if (auto exp = read_awaiter_.reset(); !exp) {
            return std::unexpected{exp.error()};
        }
        if (auto exp = write_awaiter_.reset(); !exp) {
            return std::unexpected{exp.error()};
        }

        const int rfd = std::exchange(read_fd, -1);
        const int wfd = std::exchange(write_fd, -1);

        core::Expected<> result{};
        if (rfd >= 0) {
            result = Socket::close(rfd);
        }
        // 同一个 fd 不能关两次：第二次关掉的可能是已经被复用的新 fd
        if (wfd >= 0 && wfd != rfd) {
            if (auto exp = Socket::close(wfd); !exp && result) {
                result = exp;
            }
        }
        return result;
    }

    /// 半关闭写端。先把 write_buf 里的积压排空再 shutdown —— 同步版本没法等
    /// 可写事件，只能把没发出去的数据丢掉，所以这里做成协程。
    ///
    /// 可重复调用；写端已关时是空操作。
    core::Task<core::Expected<>> shutdown();

    /// 从 fd 读一批数据进 read_buf，返回本次读到的字节数。
    /// 数据留在 read_buf 里由调用方取用；对端关闭以 BrokenPipe 返回。
    core::Task<core::Expected<size_t>> read();

    /// 依次写出多段数据，顺序即参数顺序。
    ///
    /// 缓冲为空时用 writev 一次陷入内核把多段全发出去，既不拷贝也少一次
    /// 系统调用。缓冲非空、或第一次没发完时，剩余部分按顺序追加进缓冲
    /// 统一 flush，保证字节顺序。
    core::Task<core::Expected<>>
    write_many(std::span<const std::string_view> parts);

    /// 把 data 写出去。内部缓冲还有积压时会把 data 追加进去一起排，
    /// 以保证写出顺序；返回时缓冲已排空（除非出错）。
    core::Task<core::Expected<>> write(std::string_view data = "");

    /// 记录本端地址（由外部构造后传入）。
    ///
    /// 只做记录，不发起 ::bind —— 绑定是高层动作，由连接层在合适的时机决定。
    void set_local_addr(const Address &addr) {
        sock_addr = addr;
    }

    // 初始化列表按声明顺序写，避免"看着先初始化 read_fd、实际先初始化
    // awaiter"这种误读
    Stream(Stream &&other) noexcept
        : read_awaiter_(std::move(other.read_awaiter_)),
          write_awaiter_(std::move(other.write_awaiter_)),
          read_fd(std::exchange(other.read_fd, -1)),
          write_fd(std::exchange(other.write_fd, -1)),
          sock_addr(other.sock_addr), read_buf(std::move(other.read_buf)),
          write_buf(std::move(other.write_buf)) {
    }

    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;
    Stream &operator=(Stream &&) = delete;

    ~Stream() {
        (void)close();
    }
};

inline core::Task<core::Expected<size_t>> Stream::read() {
    while (true) {
        // co_await 一个 Expected：有值就取到值，出错则把 unexpected 写进本
        // 任务的结果并冻结在这个挂起点 —— 不必手写 co_return std::unexpected。
        // 对端关闭也走这条路（Buffer::read_fd 把 EOF 报成 BrokenPipe）。
        const size_t n = co_await read_buf.read_fd(read_fd);

        if (n == 0) {
            // Buffer::read_fd 用 0 表示 EAGAIN：这一轮没数据，等可读事件再试。
            // 内层 co_await 等事件并产出 Expected<>，外层作用于它 ——
            // 等待本身失败同样自动传播
            co_await co_await read_awaiter_;
            continue;
        }

        co_return n;
    }
}

inline core::Task<core::Expected<>> Stream::write(std::string_view data) {
    const char *ptr = data.data();
    size_t remaining = data.size();

    // 缓冲里没有积压时才绕过它直写 fd；否则直接写会插到积压数据前面，
    // 打乱字节顺序
    if (write_buf.readable_bytes() == 0) {
        while (remaining > 0) {
            const ssize_t n = ::write(write_fd, ptr, remaining);
            if (n > 0) {
                ptr += n;
                remaining -= static_cast<size_t>(n);
                continue;
            }
            if (n == 0) [[unlikely]] {
                co_return core::details::make_error_to_unexpected(
                    core::details::Error::BrokenPipe);
            }
            if (errno == EINTR) {
                continue; // 被信号打断，重试而不是当失败
            }
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                write_buf.append(ptr, remaining);
                remaining = 0;
                break;
            }
            co_return core::details::from_errno_to_unexpected(errno);
        }
    } else if (!data.empty()) {
        write_buf.append(data);
    }

    co_await flush();
    co_return {};
}

inline core::Task<core::Expected<>> Stream::flush() {
    while (write_buf.readable_bytes() > 0) {
        const ssize_t n =
            ::write(write_fd, write_buf.peek(), write_buf.readable_bytes());
        if (n > 0) {
            write_buf.retrieve(static_cast<size_t>(n));
            continue;
        }
        if (n == 0) [[unlikely]] {
            co_return core::details::make_error_to_unexpected(
                core::details::Error::BrokenPipe);
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            // 等可写；等待失败由外层 co_await 自动传播成本任务的结果
            co_await co_await write_awaiter_;
            continue;
        }
        co_return core::details::from_errno_to_unexpected(errno);
    }

    co_return {};
}

inline core::Task<core::Expected<>>
Stream::write_many(std::span<const std::string_view> parts) {
    constexpr size_t kMaxIov = 8;

    if (write_buf.readable_bytes() == 0 && parts.size() <= kMaxIov) {
        std::array<iovec, kMaxIov> iov{};
        size_t cnt = 0;
        for (const auto part : parts) {
            if (part.empty()) {
                continue;
            }
            iov[cnt].iov_base = const_cast<char *>(part.data());
            iov[cnt].iov_len = part.size();
            ++cnt;
        }

        // 记下已经发出去的字节数，用它把剩余部分折进缓冲
        size_t done = 0;
        if (cnt > 0) {
            const ssize_t n =
                ::writev(write_fd, iov.data(), static_cast<int>(cnt));
            if (n > 0) {
                done = static_cast<size_t>(n);
            } else if (n == 0) [[unlikely]] {
                co_return core::details::make_error_to_unexpected(
                    core::details::Error::BrokenPipe);
            } else if (errno != EINTR && errno != EWOULDBLOCK &&
                       errno != EAGAIN) {
                co_return core::details::from_errno_to_unexpected(errno);
            }
            // EINTR / EAGAIN：done 保持 0，全部折进缓冲由 flush 重试
        }

        size_t skip = done;
        for (const auto part : parts) {
            if (skip >= part.size()) {
                skip -= part.size();
                continue;
            }
            write_buf.append(part.substr(skip));
            skip = 0;
        }

        // 常见情况：一次 writev 全发完，到这里缓冲仍是空的
        if (write_buf.readable_bytes() == 0) {
            co_return {};
        }
    } else {
        for (const auto part : parts) {
            write_buf.append(part);
        }
    }

    co_await flush();
    co_return {};
}

inline core::Task<core::Expected<>> Stream::shutdown() {
    // 先把 write_buf 里的积压排空。write("") 只排空缓冲、不追加新数据；
    // 内层 co_await 拿到 Expected<>，外层作用于它 —— 写失败自动传播
    co_await co_await write("");

    co_await write_awaiter_.reset();

    const int wfd = std::exchange(write_fd, -1);
    if (wfd < 0) {
        co_return {}; // 写端已经关过了，空操作
    }

    auto exp = Socket::shutdown(wfd);
    // 半关闭失败也要把 fd 收掉；但 wfd 与 read_fd 相同时不能关，
    // 否则读端也一起没了
    if (wfd != read_fd) {
        (void)Socket::close(wfd);
    }

    if (!exp) {
        co_return std::unexpected{exp.error()};
    }
    co_return {};
}
} // namespace netx::net::details
