#pragma once

#include "elog/logger.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/core/wrapped_task.hpp"
#include "netx/net/mpsc_queue.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <latch>
#include <list>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace netx::net::details {
class Scheduler {
  public:
    static core::Expected<Scheduler> create() {
        int wakeup_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (wakeup_fd == -1) {
            return core::details::from_errno_to_unexpected(errno);
        }

        return Scheduler(wakeup_fd);
    }

    size_t size() const noexcept {
        return sts_.size();
    }

    core::Expected<> wakeup();

    void push(core::Task<core::Expected<>> &&task) {
        task_queue_.push(std::move(task));
    }

    core::Task<core::Expected<>> scheduler_loop(std::latch &start_latch);

    ~Scheduler() {
        (void)wakeup_awaiter_.reset();
        if (wakeup_fd_ >= 0) {
            (void)::close(wakeup_fd_);
            wakeup_fd_ = -1;
        }
    }

    Scheduler(Scheduler &&other) noexcept
        : task_queue_(std::move(other.task_queue_)),
          sts_(std::move(other.sts_)),
          wakeup_fd_(std::exchange(other.wakeup_fd_, -1)),
          wakeup_awaiter_(std::move(other.wakeup_awaiter_)) {
    }

  private:
    Scheduler(int wakeup_fd)
        : wakeup_fd_(wakeup_fd),
          wakeup_awaiter_(core::details::EventLoop::loop().wait_event(
              {.fd = wakeup_fd_, .flags = core::details::Event::kEventRead})) {
    }

    core::Expected<> shallow();

    MpscQueue<core::Task<core::Expected<>>> task_queue_;
    std::list<core::details::WrappedTask<core::Task<core::Expected<>>>> sts_;

    int wakeup_fd_{-1};
    core::details::EventLoop::EventAwaiter wakeup_awaiter_;
};

inline core::Expected<> Scheduler::wakeup() {
    uint64_t signal = 1;
    while (true) {
        ssize_t n = ::write(wakeup_fd_, &signal, sizeof(uint64_t));
        if (n == sizeof(uint64_t)) {
            return {};
        }
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return {};
            }
            return core::details::from_errno_to_unexpected(errno);
        }
        return core::details::make_error_to_unexpected(
            core::details::Error::InvalidOperation);
    }
}

inline core::Expected<> Scheduler::shallow() {
    uint64_t val = 0;
    while (true) {
        ssize_t n = ::read(wakeup_fd_, &val, sizeof(uint64_t));

        if (n == sizeof(uint64_t)) {
            return {};
        }
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return {};
            }
            return core::details::from_errno_to_unexpected(errno);
        }
        return core::details::make_error_to_unexpected(
            core::details::Error::InvalidOperation);
    }
}

inline core::Task<core::Expected<>>
Scheduler::scheduler_loop(std::latch &start_latch) {
    start_latch.count_down();

    while (true) {
        // 等唤醒、读 eventfd。
        // 出错就退出整个调度循环。这里必须自己记一笔：本任务的返回值没有
        // 任何调用方会读（server 把它 push 进队列就撒手了，WrappedTask 跑完
        // 自摘节点销毁），错误会无声无息地消失 —— 表现为"服务还在跑，但
        // 这个 worker 再也不处理任何连接"，外部只能看到大片超时。
        if (auto exp = co_await wakeup_awaiter_; !exp) {
            elog::LOG_FATAL("scheduler: waiting for wakeup failed: {}; this "
                            "worker no longer serves connections",
                            exp.error().message());
            co_return std::unexpected{exp.error()};
        }

        if (auto exp = shallow(); !exp) {
            elog::LOG_FATAL("scheduler: draining the wakeup fd failed: {}; "
                            "this worker no longer serves connections",
                            exp.error().message());
            co_return std::unexpected{exp.error()};
        }

        core::Task<core::Expected<>> tmp{nullptr};
        while (task_queue_.pop(tmp)) {
            // 与 netx/core/wrapped_task.hpp:DeleteNodeHandle 配合
            sts_.emplace_back();
            auto it = std::prev(sts_.end());

            *it = core::details::WrappedTask(std::move(tmp), sts_, it);
        }
    }

    co_return {};
}
} // namespace netx::net::details