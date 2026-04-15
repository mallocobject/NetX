#include "elog/logger.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace elog;

std::atomic<size_t> count{0};

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
	count.fetch_add(1, std::memory_order_relaxed);
}

int main()
{
	size_t thread_count = 8;
	size_t entry_count = 1000000;
	std::vector<std::thread> threads;

	auto start = std::chrono::high_resolution_clock::now();

	for (size_t i = 0; i < thread_count; ++i)
	{
		threads.emplace_back(
			[entry_count]() mutable
			{
				while (entry_count--)
				{
					func();
				}
			});
	}

	for (auto& t : threads)
	{
		t.join();
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto elapsed_ms =
		std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
			.count();
	double elapsed_sec = elapsed_ms / 1000.0;

	size_t total = thread_count * entry_count;
	double throughput = total / elapsed_sec;

	std::cout << "threads: " << thread_count << std::endl;
	std::cout << "entries per thread: " << entry_count << std::endl;
	std::cout << "total entries: " << total << std::endl;
	std::cout << "count: " << count.load() << std::endl;
	std::cout << "elapsed: " << elapsed_ms << " ms" << std::endl;
	std::cout << "throughput: " << throughput / 1000000.0 << " M ops/sec"
			  << std::endl;

	return 0;
}
