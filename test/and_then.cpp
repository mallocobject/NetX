#include "netx/async/and_then.hpp"
#include "netx/async/async_main.hpp"
#include "netx/async/sleep.hpp"
#include "netx/async/task.hpp"

using namespace netx::async;
using namespace std::chrono_literals;

Task<> t1()
{
	std::cout << "t1" << std::endl;
	co_await sleep(2s);
}

Task<> t2()
{
	std::cout << "t2" << std::endl;
	co_await sleep(2s);
	std::cout << "t2" << std::endl;
	co_return;
}

Task<> t2(int a)
{
	std::cout << "t2" << std::endl;
	co_await sleep(2s);
	std::cout << a << std::endl;
	co_return;
}

int main()
{
	async_main(and_then(t1(), t2(5)));
}