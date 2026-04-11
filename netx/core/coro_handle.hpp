#pragma once

#include "netx/core/event_loop.hpp"
#include "netx/core/handle.hpp"
namespace netx
{
namespace core
{
namespace details
{
struct CoroHandle : Handle
{
	// void schedule()
	// {
	// 	if (state == Handle::State::kUnscheduled ||
	// 		state == Handle::State::kSuspend)
	// 	{
	// 		EventLoop::loop().call_soon(*this);
	// 	}
	// }

	// void cancel()
	// {
	// 	if (state == Handle::State::kScheduled ||
	// 		state == Handle::State::kSuspend)
	// 	{
	// 		EventLoop::loop().cancel(*this);
	// 	}
	// }

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
		if (state != Handle::State::kCancelled)
		{
			EventLoop::loop().cancel(*this);
		}
	}
};
} // namespace details
} // namespace core
} // namespace netx