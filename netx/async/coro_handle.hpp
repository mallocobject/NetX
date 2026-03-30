#ifndef NETX_ASYNC_CORO_HANDLE_HPP
#define NETX_ASYNC_CORO_HANDLE_HPP

#include "netx/async/event_loop.hpp"
#include "netx/async/handle.hpp"
#include <cstddef>
#include <format>
#include <source_location>
#include <string>
namespace netx
{
namespace async
{
struct CoroHandle : public Handle
{
	std::string frameName() const
	{
		const auto& frame_info = frameInfo();
		return std::format("{} at {}:{}", frame_info.function_name(),
						   frame_info.file_name(), frame_info.line());
	}

	virtual void dumpBackTnetxe(std::size_t depth = 0) const
	{
	}

	void schedule()
	{
		if (state_ == Handle::State::kUnScheduled ||
			state_ == Handle::State::kSuspend)
		{
			EventLoop::loop().call_soon(*this);
		}
	}
	void cancel()
	{
		if (state_ != Handle::State::kCancelled)
		{
			EventLoop::loop().cancel_handle(*this);
		}
	}

  private:
	virtual const std::source_location& frameInfo() const
	{
		static const std::source_location frame_info =
			std::source_location::current();
		return frame_info;
	}
};
} // namespace async
} // namespace netx

#endif