#pragma once

#include "netx/core/check_error.hpp"
#include "netx/core/event.hpp"
#include "netx/core/expected.hpp"
#include <cassert>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

namespace netx::core::details {
class Epoller {
  private:
    std::vector<epoll_event> evs_{1};
    std::size_t registered_{0};
    /// epoll 实例，只由本类持有和使用：外面既没有理由读它，读到也没法安全
    /// 操作（close/ctl 的账都在这里记）。
    const int epfd{-1};

  public:
    Epoller() : epfd(check_error<>(epoll_create1(0))) {
    }

    Epoller(Epoller &&) = delete;
    ~Epoller() {
        assert(epfd >= 0);
        close(epfd);
    }

    auto register_event(const Event &event) -> Expected<> {
        if (auto exp = ctl(EPOLL_CTL_ADD, event); !exp) {
            return exp;
        }

        ++registered_;
        if (evs_.size() < registered_) {
            evs_.resize(evs_.size() * 2);
        }

        return {};
    }

    auto modify_event(const Event &event) -> Expected<> {
        return ctl(EPOLL_CTL_MOD, event);
    }

    auto unregister_event(const Event &event) -> Expected<> {
        if (auto exp = ctl(EPOLL_CTL_DEL, event); !exp) {
            return exp;
        }

        --registered_;
        return {};
    }

    /// 返回就绪的 fd 列表。交给内核的只有 fd
    auto poll(int timeout) -> Expected<std::vector<int>> {
        int nevs = epoll_wait(
            epfd, evs_.data(), static_cast<int>(evs_.size()), timeout);
        if (nevs == -1) {
            if (errno == EINTR) {
                return {};
            }
            return from_errno_to_unexpected(errno);
        } else if (nevs == 0) {
            return {};
        }

        std::vector<int> result;
        result.reserve(nevs);
        for (int i = 0; i < nevs; ++i) {
            result.push_back(evs_[i].data.fd);
        }

        return result;
    }

  private:
    auto ctl(int op, const Event &event) -> Expected<> {
        epoll_event ev{.events = event.flags | EPOLLONESHOT,
                       .data{.fd = event.fd}};
        if (epoll_ctl(epfd, op, event.fd, &ev) == -1) {
            return from_errno_to_unexpected(errno);
        }

        return {};
    }
};
} // namespace netx::core::details