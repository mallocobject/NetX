#pragma once

#include "netx/core/check_error.hpp"
#include "netx/core/error.hpp"
#include "netx/core/event.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/handle.hpp"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
namespace netx
{
namespace core
{
namespace details
{
struct EpollPoller
{
	EpollPoller()
		: epfd(check_error<>(epoll_create1(0))), evs_(registered_event_count_)
	{
	}
	EpollPoller(EpollPoller&&) = delete;
	~EpollPoller()
	{
		assert(epfd >= 0);
		close(epfd);
	}

	constexpr bool stopped() const noexcept
	{
		return registered_event_count_ <= 1;
	}

	Expected<> register_event(const Event& event)
	{
		epoll_event ev{.events = event.flags | EPOLLONESHOT,
					   .data{.ptr = const_cast<HandleInfo*>(&event.info)}};
		int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, event.fd, &ev);
		if (ret == -1)
		{
			return from_errno(errno);
		}
		registered_event_count_++;
		if (evs_.size() < registered_event_count_)
		{
			evs_.resize(evs_.size() * 2);
		}

		return {};
	}

	Expected<> modify_event(const Event& event)
	{
		epoll_event ev{.events = event.flags | EPOLLONESHOT,
					   .data{.ptr = const_cast<HandleInfo*>(&event.info)}};
		int ret = epoll_ctl(epfd, EPOLL_CTL_MOD, event.fd, &ev);
		if (ret == -1)
		{
			return from_errno(errno);
		}

		return {};
	}

	Expected<> unregister_event(const Event& event)
	{
		int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, event.fd, nullptr);
		if (ret == -1)
		{
			return from_errno(errno);
		}
		registered_event_count_--;

		return {};
	}

	Expected<std::vector<Event>> poll(int timeout)
	{
		int nevs = epoll_wait(epfd, evs_.data(), static_cast<int>(evs_.size()),
							  timeout);
		if (nevs == -1)
		{
			if (errno == EINTR)
			{
				return {};
			}
			check_error<>(nevs);
			return from_errno(errno);
		}

		if (nevs == 0)
		{
			// return {Error::Timeout};
			return {};
		}

		std::vector<Event> result;
		result.reserve(nevs);
		for (int i = 0; i < nevs; ++i)
		{
			const auto& ev = evs_[i];
			auto info = reinterpret_cast<HandleInfo*>(ev.data.ptr);
			if (info && info->handle)
			{
				result.push_back({.info = *info});
			}
		}

		return result;
	}

	int epfd{-1};

  private:
	std::uint64_t registered_event_count_{1};
	std::vector<epoll_event> evs_;
};
} // namespace details
} // namespace core
} // namespace netx