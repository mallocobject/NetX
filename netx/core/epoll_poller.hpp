#pragma once

#include "netx/core/check_error.hpp"
#include "netx/core/event.hpp"
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

	void register_event(const Event& event)
	{
		epoll_event ev{.events = event.flags | EPOLLONESHOT,
					   .data{.ptr = const_cast<HandleInfo*>(&event.info)}};
		check_error<>(epoll_ctl(epfd, EPOLL_CTL_ADD, event.fd, &ev));
		registered_event_count_++;
		if (evs_.size() < registered_event_count_)
		{
			evs_.resize(evs_.size() * 2);
		}
	}

	void modify_event(const Event& event)
	{
		epoll_event ev{.events = event.flags | EPOLLONESHOT,
					   .data{.ptr = const_cast<HandleInfo*>(&event.info)}};
		check_error<>(epoll_ctl(epfd, EPOLL_CTL_MOD, event.fd, &ev));
	}

	void unregister_event(const Event& event)
	{
		check_error<>(epoll_ctl(epfd, EPOLL_CTL_DEL, event.fd, nullptr));
		registered_event_count_--;
	}

	std::vector<Event> poll(int timeout)
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
		}

		if (nevs == 0)
		{
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
} // namespace core
} // namespace netx