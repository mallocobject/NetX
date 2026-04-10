#include "elog/logger.hpp"
#include "netx/core/task.hpp"
#include <string>
#include <system_error>
namespace netx
{
namespace core
{

// 1. 定义你的错误码枚举
enum class MyNetError
{
	Success = 0,
	ConnectionReset,
	PacketCorrupted,
	AuthFailed
};

// 2. 创建错误类别类 (Error Category)
class MyNetErrorCategory : public std::error_category
{
  public:
	const char* name() const noexcept override
	{
		return "MyNetError";
	}

	std::string message(int ev) const override
	{
		switch (static_cast<MyNetError>(ev))
		{
		case MyNetError::ConnectionReset:
			return "Connection was reset by peer";
		case MyNetError::PacketCorrupted:
			return "Received a corrupted packet";
		case MyNetError::AuthFailed:
			return "Authentication failed";
		default:
			return "Unknown error";
		}
	}
};

// 3. 获取单例类别
const std::error_category& get_my_net_error_category()
{
	static MyNetErrorCategory instance;
	return instance;
}

// 4. 重载 make_error_code 函数 (必须在枚举所在的命名空间)
inline std::error_code make_error_code(MyNetError e)
{
	return {static_cast<int>(e), get_my_net_error_category()};
}

} // namespace core
} // namespace netx

// 5. 注册到 std 命名空间，允许隐式转换
namespace std
{
template <> struct is_error_code_enum<netx::core::MyNetError> : true_type
{
};

using namespace netx::core;

// 模拟一个可能会失败的底层任务
Task<Expected<int>> fetchData()
{
	// 模拟返回自定义错误
	co_return MyNetError::PacketCorrupted;
}

// 模拟业务逻辑任务
Task<Expected<>> businessLogic()
{
	elog::LOG_INFO("开始业务逻辑...");

	// --- 方式 A: 手动检查错误 ---
	auto res = co_await fetchData(); // 只 co_await 任务本身，得到 Expected<int>

	if (!res.has_value())
	{
		const std::error_code& ec = res.error();
		elog::LOG_WARN("检测到错误: {} (错误码: {}, 类别: {})", ec.message(),
					   ec.value(), ec.category().name());

		if (ec == MyNetError::PacketCorrupted)
		{
			elog::LOG_INFO("尝试针对数据损坏进行修复...");
		}
		// 可以选择在这里处理错误，或者继续向下执行
	}
	else
	{
		elog::LOG_INFO("获取到数据: {}", res.value());
	}

	// --- 方式 B: 自动传播 (短路) ---
	// 如果 fetchData 返回错误，下面的代码将永远不会执行
	// 错误会被自动填入 businessLogic 的 promise 并向上返回
	int val = co_await co_await fetchData();

	elog::LOG_INFO("这行日志不会打印，因为上面的 fetchData 报错了: {}", val);
	co_return Expected<>{};
}

int main()
{
	auto t = businessLogic();
	t.coro.promise().schedule();
	details::EventLoop::loop().run_until_complete();
	return 0;
}
} // namespace std