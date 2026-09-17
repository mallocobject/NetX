#pragma once

#include "elog/logger.hpp"
#include "netx/core/async_main.hpp"
#include "netx/core/event.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/net/address.hpp"
#include "netx/net/scheduler.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <functional>
#include <latch>
#include <limits>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <semaphore>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace netx::net::details {
// 服务端骨架：监听、accept、把连接分发给工作线程。派生类只需提供
// handle_client(int fd1, int fd2) -> Task<Expected<>>。
class Server {
  public:
    Server(Server &&) = delete;

    ~Server() {
        for (int &fd : idle_fd_) {
            if (fd >= 0) {
                (void)::close(fd);
                fd = -1;
            }
        }
    }

    /// 注册"新连接"回调。不设就不回调。
    auto &on_accept(this auto &self,
                    std::function<void(int, const Address &)> cb) {
        self.on_accept_ = std::move(cb);
        return self;
    }

    /// 同时可服务的客户端连接数上限。置零的不限
    auto &max_connections(this auto &self, size_t n) {
        if (self.sticky_error_) {
            return self;
        }

        if (n == 0) {
            return self;
        }

        self.max_connections_ = n;

        self.slots_.emplace(static_cast<std::ptrdiff_t>(std::min(
            n, static_cast<size_t>(std::counting_semaphore<>::max()))));
        return self;
    }

    auto &listen(this auto &self, const Address &sock_addr) {
        if (self.sticky_error_) {
            return self;
        }

        const int listen_fd = self.stream_.read_fd;

        const auto result =
            Socket::set_reuse_addr(listen_fd)
                .and_then([listen_fd, &sock_addr] {
                    return Socket::bind(listen_fd, sock_addr);
                })
                .and_then([listen_fd] { return Socket::listen(listen_fd); });

        if (!result) {
            self.sticky_error_ = result.error();
            elog::LOG_FATAL("Listen failed at {}: {}, {}",
                            sock_addr.to_formatted_string(),
                            self.sticky_error_.value(),
                            self.sticky_error_.message());
            return self;
        }

        // 记录**实际**绑定的地址，而不是入参：端口传 0 时由内核挑一个，
        // 存 0 的话对外报出来的是无意义的 "0.0.0.0:0"
        if (auto bound = Socket::get_socket_name(listen_fd); bound) {
            self.stream_.set_local_addr(*bound);
        } else {
            self.stream_.set_local_addr(sock_addr);
        }
        return self;
    }

    // Address 解析失败会抛 std::system_error；这里折进 sticky_error_，
    auto &listen(this auto &self, const std::string &ip, uint16_t port) {
        try {
            return self.listen(Address{ip, port});
        } catch (const std::system_error &e) {
            self.sticky_error_ = e.code();
            elog::LOG_FATAL("Listen failed at {}:{}: {}",
                            ip,
                            port,
                            self.sticky_error_.message());
            return self;
        }
    }

    auto &listen(this auto &self, uint16_t port) {
        return self.listen(Address{port});
    }

    template <typename Rep, typename Period>
    auto &timeout(this auto &self,
                  std::chrono::duration<Rep, Period> duration) {
        if (self.sticky_error_) {
            return self;
        }

        self.timeout_ =
            std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
        return self;
    }

    auto &loop(this auto &self, size_t loop_count = 1) {
        if (self.sticky_error_) {
            return self;
        }

        if (loop_count < 1) {
            self.sticky_error_ = core::details::make_error_code(
                core::details::Error::InvalidOperation);
            elog::LOG_FATAL("loop count must be >= 1, got {}", loop_count);
            return self;
        }

        self.loop_count_ = loop_count;
        return self;
    }

    // 链式调用收尾环节
    void start(this auto &self);

  protected:
    explicit Server(Stream &&stream, int idle_fd1, int idle_fd2)
        : stream_(std::move(stream)), idle_fd_{idle_fd1, idle_fd2} {
    }

    /// 取一个连接名额。
    ///   true  —— 还有余额（当前连接数 < 上限），可以正常接纳这条连接
    ///   false —— 已达上限，调用方应当把这条连接关掉
    /// 没设上限时永远为 true。
    [[nodiscard]] bool try_acquire_slot(this auto &self) {
        return !self.slots_ || self.slots_->try_acquire();
    }

    /// 是否配置了连接空闲超时。
    /// timeout_ 的默认值就是"不超时"那个哨兵，所以两者相等即表示没设。
    [[nodiscard]] bool has_timeout() const noexcept {
        return timeout_ < kNoTimeout;
    }

    /// 连接关闭时由派生类调用，归还一个名额。(原子)
    void release_connection(this auto &self) {
        if (self.slots_) {
            self.slots_->release();
        }
    }

    template <typename Self>
    core::Task<core::Expected<>> server_loop(this Self &self);

    Stream stream_;
    size_t loop_count_{1};

    std::vector<std::unique_ptr<Scheduler>> schedulers_;
    std::mutex mtx_;
    std::vector<std::jthread> loops_;

    int idle_fd_[2] = {-1, -1};

    // 最长挂机一年
    inline static constexpr std::chrono::hours kNoTimeout{24 * 365};
    std::chrono::nanoseconds timeout_{kNoTimeout};

    std::error_code sticky_error_;

    size_t next_worker_{0};
    int emfile_count_{0};

    // ---- 并发连接数上限 ----
    inline static constexpr size_t kUnlimitedConnections =
        std::numeric_limits<size_t>::max();

    size_t max_connections_{kUnlimitedConnections};

    // 连接名额。计数信号量正好就是"还剩几个名额"这个语义：
    //   accept 之后 try_acquire 取一个，取不到就把这条连接直接关掉 ——
    //   接了再关，对端立刻拿到反馈，而不是在内核 backlog 里干等。
    //   连接关闭时由派生类调 release_connection() 还一个。
    // 用 optional 是因为初值只能在配置完之后、start() 之前才定得下来，
    // 而 counting_semaphore 不可移动、也没法重新赋值。
    // 不设上限时整个不建，省掉每次都取信号量的开销。
    std::optional<std::counting_semaphore<>> slots_;

    std::function<void(int fd, const Address &peer)> on_accept_;
};

inline void Server::start(this auto &self) {
    if (self.sticky_error_) {
        elog::LOG_FATAL("Server cannot start due to previous errors: {}",
                        self.sticky_error_.message());
        return;
    }

    // 忽略 SIGPIPE，避免对已关闭连接写入导致进程退出
    std::signal(SIGPIPE, SIG_IGN);

    // EMFILE 自救靠"先关掉预留 fd 腾名额"
    if (self.idle_fd_[0] < 0) {
        self.idle_fd_[0] = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        self.idle_fd_[1] =
            (self.idle_fd_[0] >= 0) ? ::dup(self.idle_fd_[0]) : -1;
    }

    auto *srv = &self;
    std::latch start_latch(self.loop_count_);

    for (size_t idx = 0; idx < self.loop_count_; ++idx) {
        self.loops_.emplace_back([srv, &start_latch] {
            auto exp = Scheduler::create();
            if (!exp) {
                start_latch.count_down();
                std::lock_guard<std::mutex> lock(srv->mtx_);
                srv->sticky_error_ = exp.error();
                return;
            }

            Scheduler *raw = nullptr;
            {
                std::lock_guard<std::mutex> lock(srv->mtx_);
                srv->schedulers_.push_back(
                    std::make_unique<Scheduler>(std::move(*exp)));
                raw = srv->schedulers_.back().get();
            }

            (void)core::async_main(raw->scheduler_loop(start_latch));
        });
    }

    start_latch.wait();

    elog::LOG_WARN("NetX-Server listening on {}",
                   self.stream_.sock_addr.to_formatted_string());

    core::async_main(self.server_loop());
}

template <typename Self>
core::Task<core::Expected<>> Server::server_loop(this Self &self) {
    const int listen_fd = self.stream_.read_fd;
    auto ev_awaiter =
        core::details::EventLoop::loop().wait_event(core::details::Event{
            .fd = listen_fd, .flags = core::details::Event::kEventRead});

    while (true) {
        if (auto exp = co_await ev_awaiter; !exp) {
            elog::LOG_FATAL("Server: waiting on the listen fd failed: {}",
                            exp.error().message());
            co_return std::unexpected{exp.error()};
        }

        while (true) {
            Address addr;
            auto exp = Socket::accept(listen_fd, &addr);

            if (!exp) {
                // 用 errno 而不是 exp.error()：from_errno 会把 EMFILE 之类
                // 映射成库自己的错误码，原始值只在 errno 里还留着
                switch (errno) {
                case EAGAIN:
                case EINTR:
                case ECONNABORTED:
                    break;

                case EMFILE: {
                    // fd 用尽时的标准自救：先关掉预留的 idle fd 腾出名额，
                    // 把已经排队的连接接进来立刻关掉（让对端拿到 RST 而不是
                    // 干等），再重新占住 /dev/null 备用
                    ++self.emfile_count_;
                    if (self.emfile_count_ > 5) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        self.emfile_count_ = 0;
                    }

                    (void)Socket::close(self.idle_fd_[0]);
                    (void)Socket::close(self.idle_fd_[1]);

                    if (auto pending = Socket::accept(listen_fd, &addr);
                        pending) {
                        (void)Socket::close(*pending);
                    }

                    self.idle_fd_[0] =
                        ::open("/dev/null", O_RDONLY | O_CLOEXEC);
                    self.idle_fd_[1] =
                        (self.idle_fd_[0] >= 0) ? ::dup(self.idle_fd_[0]) : -1;

                    elog::LOG_WARN("Server: out of file descriptors (EMFILE)");
                    break;
                }

                case EBADF:
                case EINVAL:
                case ENOTSOCK:
                    // listen socket 已经不可用（被关掉/类型不对）。epoll 会
                    // 一直报可读，再转下去就是满速空转刷日志 —— 收掉整个
                    // 循环，把错误交给 start() 的调用方
                    elog::LOG_FATAL("Server: fatal accept error: {}",
                                    exp.error().message());
                    co_return std::unexpected{exp.error()};

                default:
                    elog::LOG_WARN("Server: accept failed: {}",
                                   exp.error().message());
                    break;
                }
                break; // 回到外层，等下一次可读事件
            }

            const int conn_fd = *exp;

            // 并发已满：接进来立刻关掉。对端马上拿到 RST
            if (!self.try_acquire_slot()) {
                (void)Socket::close(conn_fd);
                elog::LOG_WARN(
                    "Server: connection limit ({}) reached, rejected fd={}",
                    self.max_connections_,
                    conn_fd);
                continue;
            }

            const int dup_conn_fd = ::dup(conn_fd);
            // dup 失败
            if (dup_conn_fd < 0) {
                const auto ec = core::details::from_errno(errno);
                (void)Socket::close(conn_fd);
                if (self.slots_) {
                    self.slots_->release(); // 名额还回去
                }
                elog::LOG_WARN(
                    "Server: dup failed for fd={}: {}", conn_fd, ec.message());
                continue;
            }

            // 关掉 Nagle：失败不致命，只是小报文多等一个 RTT
            (void)Socket::set_no_delay(conn_fd);

            // 一个 worker 都没起来时不能取模（除零），只能把连接关掉
            if (self.schedulers_.empty()) [[unlikely]] {
                (void)Socket::close(conn_fd);
                (void)Socket::close(dup_conn_fd);
                if (self.slots_) {
                    self.slots_->release();
                }
                continue;
            }

            Scheduler *lucky =
                self.schedulers_[self.next_worker_++ % self.schedulers_.size()]
                    .get();

            elog::LOG_DEBUG("Server: accepted fd={} addr={}",
                            conn_fd,
                            addr.to_formatted_string());

            if (self.on_accept_) {
                self.on_accept_(conn_fd, addr);
            }

            lucky->push(self.handle_client(conn_fd, dup_conn_fd));
            (void)lucky->wakeup();
        }
    }

    co_return {};
}
} // namespace netx::net::details
