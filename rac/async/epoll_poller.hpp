#ifndef RAC_ASYNC_EPOLL_POLLER_HPP
#define RAC_ASYNC_EPOLL_POLLER_HPP

#include "rac/async/check_error.hpp"
#include "rac/async/event.hpp"
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
namespace rac
{
struct EpollPoller
{
	EpollPoller() : epfd_(checkError(epoll_create1(0)))
	{
	}

	EpollPoller(EpollPoller&&) = delete;

	~EpollPoller()
	{
		assert(epfd_ > 0);
		close(epfd_);
	}

	bool stopped() const noexcept
	{
		return registered_event_count_ == 1;
		// return false;
	}

	void registerEvent(const Event& event)
	{
		epoll_event ev{
			.events = event.flags | EPOLLONESHOT,
			.data{.ptr = const_cast<HandleInfo*>(&event.handle_info)}};
		checkError(epoll_ctl(epfd_, EPOLL_CTL_ADD, event.fd, &ev));
		registered_event_count_++;
	}

	void modifyEvent(const Event& event)
	{
		epoll_event ev{
			.events = event.flags | EPOLLONESHOT,
			.data{.ptr = const_cast<HandleInfo*>(&event.handle_info)}};
		checkError(epoll_ctl(epfd_, EPOLL_CTL_MOD, event.fd, &ev));
	}

	void unregisterEvent(const Event& event)
	{
		checkErrorNonBlock<EBADF, ENOENT>(
			epoll_ctl(epfd_, EPOLL_CTL_DEL, event.fd, nullptr));
		registered_event_count_--;
	}

	std::vector<Event> poll(int timeout)
	{
		std::vector<epoll_event> evs(registered_event_count_);
		int nevs = checkErrorNonBlock<EINTR>(
			epoll_wait(epfd_, evs.data(), registered_event_count_, timeout));

		std::vector<Event> result;
		for (int i = 0; i < nevs; i++)
		{
			auto handle_info = reinterpret_cast<HandleInfo*>(evs[i].data.ptr);
			if (handle_info->bootstrap_fd != -1)
			{
				int fd = handle_info->bootstrap_fd;
				delete handle_info; // 释放内存

				epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);

				co_spawn(handleClient(fd));
			}
			else if (handle_info->handle)
			{
				result.emplace_back(*handle_info);
			}
		}

		return result;
	}

	int epfd() const noexcept
	{
		return epfd_;
	}

  private:
	int epfd_{-1};
	std::uint64_t registered_event_count_{1};
	// at least one space for epoll_wait
};
} // namespace rac

#endif