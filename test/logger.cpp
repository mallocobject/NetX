#include "elog/logger.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

using namespace elog;

std::atomic<size_t> count = 0;

void func()
{
	thread_local size_t idx = 0;
	std::string str = "123456789zbcdefghijklmnopqrstuvwxwy";
	++idx %= 5;

	switch (idx)
	{
	case 0:
		LOG_TRACE("{}", str);
		break;
	case 1:
		LOG_DEBUG("{}", str);
		break;
	case 2:
		LOG_INFO("{}", str);
		break;
	case 3:
		LOG_WARN("{}", str);
		break;
	case 4:
		LOG_ERROR("{}", str);
		break;
	}
	count.fetch_add(1, std::memory_order_acq_rel);
}

int main()
{
	size_t thread_count = 8;
	size_t entry_count = 1;
	std::vector<std::thread> thread_pool(8);

	for (auto& thread : thread_pool)
	{
		thread = std::thread(
			[entry_count]() mutable
			{
				while (entry_count--)
				{
					func();
				}
			});
	}

	for (auto& thread : thread_pool)
	{
		thread.join();
	}

	std::cout << "expected count: " << thread_count * entry_count << std::endl;
	std::cout << "count: " << count << std::endl;

	std::this_thread::sleep_for(std::chrono::seconds(10));
}