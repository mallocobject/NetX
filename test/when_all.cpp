#include "netx/async/when_all.hpp"
#include "elog/logger.hpp"
#include "netx/async/async_main.hpp"
#include "netx/async/non_void_helper.hpp"
#include "netx/async/sleep.hpp"
#include "netx/async/task.hpp"
#include <stdexcept>

using namespace netx::async;
using namespace elog;
using namespace std::chrono_literals;

Task<> task1()
{
	co_await sleep(5s);
	LOG_INFO("hello");
	// throw std::runtime_error("exception");
	co_return;
}

Task<int> task2()
{
	co_await when_all(sleep(2s), task1());
	LOG_INFO("world");
	co_return 4;
}

Task<> task3()
{
	auto v = co_await when_all(sleep(1s), when_all(sleep(3s), task2()));
	LOG_INFO("{}", is_non_void(std::get<1>(v)));
	LOG_INFO("{}", std::get<1>(std::get<1>(v)));
	LOG_INFO("{}", is_non_void(std::get<1>(std::get<1>(v))));

	LOG_INFO("hello world");
}

int main()
{
	auto start = std::chrono::steady_clock::now();
	async_main(task3());
	LOG_INFO("{}", std::chrono::duration_cast<std::chrono::seconds>(
					std::chrono::steady_clock::now() - start)
					.count());
}