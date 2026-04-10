#pragma once

#include "netx/core/concepts.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/scheduled_task.hpp"
#include <utility>
namespace netx
{
namespace core
{
inline void async_main()
{
	details::EventLoop::loop().run_until_complete();
}

template <details::Future Fut> decltype(auto) async_main(Fut&& fut)
{
	auto tmp = details::ScheduledTask{std::move(fut)};
	async_main();
	return std::move(tmp).result();
}
} // namespace core
} // namespace netx