#include "rac/async/when_any.hpp"
#include "elog/logger.h"
#include "rac/async/async_main.hpp"
#include "rac/async/sleep.hpp"
#include "rac/async/task.hpp"
#include <coroutine>
#include <thread>

using namespace rac;
using namespace std::chrono_literals;

Task<> task1()
{
	co_await sleep(1s);
	LOG_INFO << "hello";
	co_return;
}

Task<> task2()
{
	co_await when_any(sleep(2s), task1());
	LOG_INFO << "world";
}

Task<> task3()
{
	co_await when_any(sleep(4s), when_any(sleep(3s), task2()));
	LOG_INFO << "hello world";
}

Task<> forever()
{
	co_await when_any(std::suspend_always{}, sleep(1s));
}

Task<> rightnow()
{
	co_await when_any(std::suspend_never{}, sleep(1s));
}

int main()
{
	auto start = std::chrono::steady_clock::now();
	async_main(rightnow());
	LOG_INFO << std::chrono::duration_cast<std::chrono::seconds>(
					std::chrono::steady_clock::now() - start)
					.count();
}