#include "netx/meta/lock_free_queue.hpp"
#include <atomic>
#include <iostream>
#include <thread>

using namespace netx::meta;

int main()
{
	constexpr int PER_PRODUCER = 10000000; // 每个生产者生产数量

	LockFreeQueue<int> queue;
	std::atomic<long long> sum_in{0}, sum_out{0};
	std::atomic<int> producers_done{0};
	std::atomic<bool> finished{false};

	auto producer = [&]
	{
		for (int i = 0; i < PER_PRODUCER; ++i)
		{
			queue.push(i);
			sum_in += i;
		}
		++producers_done;
	};

	auto consumer = [&]
	{
		int val;
		while (true)
		{
			if (queue.pop(val))
			{
				sum_out += val;
			}
			else if (finished.load(std::memory_order_acquire))
			{
				break;
			}
			else
			{
				std::this_thread::yield(); // 避免忙等
			}
		}
	};

	std::thread prod, con;
	prod = std::thread(producer);
	con = std::thread(consumer);

	prod.join();
	finished.store(true, std::memory_order_release);
	con.join();

	std::cout << "Pushed sum: " << sum_in << "\nPopped sum: " << sum_out
			  << "\nQueue size: " << queue.size() << std::endl;
	std::cout << (sum_in == sum_out ? "✅ PASS" : "❌ FAIL") << std::endl;
	return 0;
}