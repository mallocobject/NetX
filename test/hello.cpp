#include "rac/async/awaitable_traits.hpp"
#include "rac/async/check_error.hpp"
#include "rac/async/concepts.hpp"
#include "rac/async/coro_handle.hpp"
#include "rac/async/epoll_poller.hpp"
#include "rac/async/event.hpp"
#include "rac/async/event_loop.hpp"
#include "rac/async/handle.hpp"
#include "rac/async/non_void_helper.hpp"
#include "rac/async/task.hpp"
#include <iostream>
#include <rac/async/async_main.hpp>
#include <string>

using namespace rac;

Task<std::string_view> hello()
{
	co_return "hello";
}

Task<std::string_view> world()
{
	co_return "world";
}

Task<std::string> hello_world()
{
	co_return std::format("{} {}", co_await hello(), co_await world());
}

int main()
{
	std::cout << std::format("run result: {}\n", async_main(hello_world()));

	return 0;
}