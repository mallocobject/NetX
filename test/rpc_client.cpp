#include "elog/logger.hpp"
#include "netx/async/async_main.hpp"
#include "netx/async/task.hpp"
#include "netx/async/when_all.hpp"
#include "netx/rpc/client.hpp"
#include <thread>
#include <csignal>

using namespace netx::async;
using namespace netx::rpc;
using namespace std::chrono_literals;
using namespace elog;

struct Point
{
	double x;
	double y;
};

struct User
{
	std::uint32_t id;
	std::string name;
	Point loc;

	std::string email;
	std::uint64_t phone_number;
};

Task<> connect_server(RpcClient& client)
{
	try
	{
		while (true)
		{
			auto [pt, usr] = co_await when_all(
				client.call<Point>("AddPoint", Point{1, 2}, Point{5.2, 6.8}),
				client.call<User>("EchoUser", User{8, "liuna", {1, 5}}));

			std::cout << "x: " << pt.x << std::endl;
			std::cout << "y: " << pt.y << std::endl;

			std::cout << "ID: " << usr.id << "\n";
			std::cout << "Name: " << usr.name << "\n";
			std::cout << "Location: (" << usr.loc.x << ", " << usr.loc.y
					  << ")\n";

			std::this_thread::sleep_for(1s);
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("{}", e.what());
		client.close();
	}
}

int main()
{
	::signal(SIGPIPE, SIG_IGN);

	RpcClient client{"127.0.0.1", 8080};
	async_main(connect_server(client));
}