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
#include <netinet/tcp.h>
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
  protected:
    Stream stream_;
    size_t loop_count_{1};

    // 工作线程的调度器。必须用 unique_ptr 持有：它们要活得比所绑定的
    // 线程久，裸指针在线程退出后就悬垂了。
    std::vector<std::unique_ptr<Scheduler>> schedulers_;
    std::mutex mtx_;
    std::vector<std::jthread> loops_;

    int idle_fd_[2] = {-1, -1};

    // "不超时"的哨兵值。不能用 nanoseconds::max()：call_after 算的是
    // Clock::now() + duration，int64 纳秒再加 max() 会溢出成负数，截止时间
    // 落到过去，定时器立刻触发而不是"很久以后"。一年离溢出边界余量足够。
    inline static constexpr std::chrono::hours kNoTimeout{24 * 365};
    std::chrono::nanoseconds timeout_{kNoTimeout};

    std::error_code sticky_error_;

    // 轮转下标与 EMFILE 退避计数。做成成员而不是函数内 static：static 是
    // 全程序一份，多个 Server 实例会互相干扰。
    size_t next_worker_{0};
    int emfile_count_{0};

    // ---- 并发连接数上限 ----
    //
    // 无上限用这个哨兵值，而不是 0：0 是"一个都不许连"，语义不同
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

    std::function<void(int fd, const Address &peer)> on_accept_;

    /// 连接关闭时由派生类调用，归还一个名额。
    /// 可以跨线程调用 —— counting_semaphore::release() 本身就是原子的，
    /// 所以这里不需要额外的唤醒通道。
    void release_connection(this auto &self) {
        if (self.slots_) {
            self.slots_->release();
        }
    }

    template <typename Self>
    core::Task<core::Expected<>> server_loop(this Self &self);

  public:
    Server(Server &&) = delete;

    /// 先注销 awaiter 再关 fd：析构体跑在成员析构之前，顺序反了就会拿一个
    /// 已经关闭的 fd 去 epoll 注销。
    ~Server() {
        for (int &fd : idle_fd_) {
            if (fd >= 0) {
                (void)::close(fd);
                fd = -1;
            }
        }
    }

    /// 注册"新连接"回调。不设就不回调。
    ///
    /// 给应用层留的挂钩：库内部那条 accepted 是 DEBUG 级的，终端默认门限
    /// 看不见，而"新连接该记到哪一级、要不要落盘"不该由库替应用决定。
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

        // 0 表示不限：保持默认哨兵值、不建信号量，accept 路径上也就没有
        // 任何额外开销
        if (n == 0) {
            return self;
        }

        self.max_connections_ = n;

        // 名额在配置时就建好，不必等到 start()：start() 是阻塞的，
        // 等到那里才建，"还有没有余额"这件事在配置之后就完全查不到、
        // 也测不了。counting_semaphore 就地构造没有额外代价。
        self.slots_.emplace(static_cast<std::ptrdiff_t>(std::min(
            n, static_cast<size_t>(std::counting_semaphore<>::max()))));
        return self;
    }

    auto &listen(this auto &self, const Address &sock_addr) {
        if (self.sticky_error_) {
            return self;
        }

        const int listen_fd = self.stream_.read_fd;

        // 三步都是 Expected<>、错误处理完全一致：交给 and_then 串联，
        // 短路由结构保证，错误路径只写一次。三步各自 early-return 也行，
        // 但那要重复三遍同样的记错误 + 记日志。
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
    // 与其它配置错误保持同一条错误通道，不把异常漏给调用方
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

    void start(this auto &self);
};

inline void Server::start(this auto &self) {
    if (self.sticky_error_) {
        elog::LOG_FATAL("Server cannot start due to previous errors: {}",
                        self.sticky_error_.message());
        return;
    }

    // 忽略 SIGPIPE，避免对已关闭连接写入导致进程退出
    std::signal(SIGPIPE, SIG_IGN);

    // EMFILE 自救靠"先关掉预留 fd 腾名额"，所以这里必须保证它们真的开着：
    // 调用方可以在构造时预置，没给就由 start() 补上。
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
                // 必须 count_down。少了这一次，start_latch.wait() 会永远
                // 等下去 —— 一个 worker 起不来就卡死整个启动流程。
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

            // 计数在 scheduler_loop 的第一句里递减，也就是登记完成之后 ——
            // 于是 start_latch.wait() 返回时，schedulers_ 一定已经填好，
            // 主线程读它不再与这里的写入竞争
            core::async_main(raw->scheduler_loop(start_latch));
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

            // 并发已满：接进来立刻关掉。对端马上拿到 RST，比让它在内核
            // backlog 里干等（等多久、会不会被丢都不可控）更早拿到反馈。
            if (!self.try_acquire_slot()) {
                (void)Socket::close(conn_fd);
                elog::LOG_WARN(
                    "Server: connection limit ({}) reached, rejected fd={}",
                    self.max_connections_,
                    conn_fd);
                continue;
            }

            const int dup_conn_fd = ::dup(conn_fd);
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

            int opt = 1;
            (void)::setsockopt(
                conn_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

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
            // 名额随这条连接一起交出去，不还 —— 派生类在连接关闭时调
            // release_connection() 归还
        }
    }

    co_return {};
}
} // namespace netx::net::details
