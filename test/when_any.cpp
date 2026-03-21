#include "rac/async/when_any.hpp"
#include "elog/logger.h"
#include "rac/async/async_main.hpp"
#include "rac/async/sleep.hpp"
#include "rac/async/task.hpp"

using namespace rac;
using namespace std::chrono_literals;

Task<> task1()
{
	co_await sleep(3s);
	LOG_INFO << "hello";
	co_return;
}

Task<> task2()
{
	co_await when_any(sleep(2s), task1());
	LOG_INFO << "world";
}

int main()
{
	async_main(task2());
}