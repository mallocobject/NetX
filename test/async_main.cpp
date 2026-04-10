#include "netx/core/async_main.hpp"
#include "elog/logger.hpp"
#include "netx/core/task.hpp"

using namespace netx::core;

Task<int> t1()
{
	elog::LOG_INFO("hello");
	co_return 2;
}

int main()
{
	elog::LOG_INFO("{}", async_main(t1()));
}