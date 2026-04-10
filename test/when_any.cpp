#include "netx/core/when_any.hpp"
#include "elog/logger.hpp"
#include "netx/core/async_main.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
using namespace netx::core;

Task<Expected<>> t1()
{
	elog::LOG_INFO("hello");
	co_return std::make_error_code(std::errc::argument_out_of_domain);

	co_return {};
}

Task<Expected<>> t2()
{
	co_await co_await when_any(t1());
	elog::LOG_INFO("world");
	co_return {};
}

int main()
{
	auto t = t2();
	t.coro.promise().schedule();
	async_main();
}