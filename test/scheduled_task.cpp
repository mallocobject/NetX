#include "netx/core/scheduled_task.hpp"
#include "elog/logger.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/task.hpp"

using namespace netx::core;

Task<> t1()
{
	elog::LOG_INFO("hello");
	co_return;
}

int main()
{
	auto t = co_spawn(t1());
	details::EventLoop::loop().run_until_complete();
}