#include "netx/core/sleep.hpp"
#include "elog/logger.hpp"
#include "netx/core/async_main.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/task.hpp"
#include "netx/core/when_any.hpp"
#include <chrono>
#include <unistd.h>

using namespace netx::core;
using namespace std::chrono_literals;

Task<Expected<int>> t1()
{
	elog::LOG_INFO("hello");
	co_await co_await sleep(1s);

	co_return 2;
}

Task<Expected<>> t()
{
	auto begin = std::chrono::steady_clock::now();
	auto ep = co_await when_any(t1(), sleep(2s));
	auto diff = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - begin);
	elog::LOG_WARN("{}", diff);
	if (!ep.has_value())
	{
		elog::LOG_ERROR("{}", ep.error().message());
	}
	else
	{
		if (ep.value().index() == 0)
		{
			elog::LOG_INFO("{}", std::get<0>(ep.value()).value());
		}
	}
	co_return {};
}

int main()
{
	async_main(t());
}