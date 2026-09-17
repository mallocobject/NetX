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
  public:
    Epoller() : epfd_(check_error<>(epoll_create1(0))) {
    }

    Epoller(Epoller &&) = delete;
    ~Epoller() {
        assert(epfd_ >= 0);
        close(epfd_);
    }

    Expected<> register_event(const Event &event) {
        if (auto exp = ctl(EPOLL_CTL_ADD, event); !exp) {
            return exp;
        }

        ++registered_;
        if (evs_.size() < registered_) {
            evs_.resize(evs_.size() * 2);
        }

        return {};
    }

    Expected<> modify_event(const Event &event) {
        return ctl(EPOLL_CTL_MOD, event);
    }

    Expected<> unregister_event(const Event &event) {
        if (auto exp = ctl(EPOLL_CTL_DEL, event); !exp) {
            return exp;
        }

        --registered_;
        return {};
    }

    /// 返回就绪的 fd 列表。交给内核的只有 fd
    Expected<std::vector<int>> poll(int timeout) {
        int nevs = epoll_wait(
            epfd_, evs_.data(), static_cast<int>(evs_.size()), timeout);
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
    Expected<> ctl(int op, const Event &event) {
        epoll_event ev{.events = event.flags | EPOLLONESHOT,
                       .data{.fd = event.fd}};
        if (epoll_ctl(epfd_, op, event.fd, &ev) == -1) {
            return from_errno_to_unexpected(errno);
        }

        return {};
    }

    std::vector<epoll_event> evs_{1};
    std::size_t registered_{0};
    const int epfd_{-1};
};
} // namespace netx::core::details