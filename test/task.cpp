#include "netx/core/task.hpp"
#include "elog/logger.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/expected.hpp"
#include <system_error>

using namespace netx::core;

// t1 返回一个包含错误码的 Expected
Task<Expected<int>> t1()
{
	elog::LOG_INFO("t1: 准备返回错误码...");
	// 构造一个包含 std::errc 错误码的 Expected 对象
	// 使用 std::make_error_code 转换为 std::error_code
	co_return std::make_error_code(std::errc::argument_out_of_domain);
}

Task<Expected<>> t2()
{
	// 第一个 co_await 等待 Task 完成，返回 Expected<int>
	// 第二个 co_await 触发 promise_type::await_transform(Expected<U>&&)
	// 如果 t1 返回的是错误，t2 会在这里直接挂起并退出，后续的 LOG_INFO 不会执行
	int value = co_await co_await t1();

	elog::LOG_INFO("t2: 这里的日志不会被打印，因为上面已经因为错误短路了: {}",
				   value);
	co_return Expected<>{};
}

int main()
{
	auto t = t2();

	// 获取 t2 的 promise 引用以便后续检查结果
	auto& promise = t.coro.promise();

	promise.schedule();
	EventLoop::loop().run_until_complete();

	// 检查最终结果
	// 由于 t2 被 t1 的错误“短路”了，t1 的错误会被 put_value 到 t2 的 promise 中
	if (!promise.has_value)
	{
		// 如果 Result 里存的是异常
		if (promise.exception)
		{
			try
			{
				std::rethrow_exception(promise.exception);
			}
			catch (const std::exception& e)
			{
				elog::LOG_ERROR("捕获到异常: {}", e.what());
			}
		}
	}
	else
	{
		// 在你的 Result 实现中，如果是 Expected 错误，它被当做“值”存入了
		// Result<T> 注意：这取决于你的 Result 模板如何处理 Expected 按照你
		// task.hpp 的逻辑，Expected 的错误被传给了父协程的 put_value
		elog::LOG_INFO("任务执行完毕（检查逻辑视 Result 具体实现而定）");
	}

	return 0;
}