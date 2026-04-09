#pragma once

#include "netx/core/event_loop.hpp"
#include "netx/core/handle.hpp"
namespace netx
{
namespace core
{
struct CoroHandle : Handle
{
	void schedule()
	{
		if (state == Handle::State::kUnscheduled ||
			state == Handle::State::kSuspend)
		{
			EventLoop::loop().call_soon(*this);
		}
	}

	void cancel()
	{
		if (state == Handle::State::kScheduled)
		{
			EventLoop::loop().cancel(*this);
		}
	}
};
} // namespace core
} // namespace netx