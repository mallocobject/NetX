// 把所有公开头文件都包含一遍。
//
// 存在的理由：CMake 里只有被测试真正引用的头才会被编译，netx/http/server.hpp
// 就是典型 —— 没有任何单元测试能驱动 start()，于是它一次都没被构建过。
// 结果是改动 session.hpp 里的接口后，构建和测试双双通过，坏掉的头却没人发现。
//
// 这个 TU 没有断言，它的价值在于"能编译"本身。

#include "netx/core/concepts.hpp"
#include "netx/core/coro_handle.hpp"
#include "netx/core/epoller.hpp"
#include "netx/core/event.hpp"
#include "netx/core/event_loop.hpp"
#include "netx/core/expected.hpp"
#include "netx/core/handle.hpp"
#include "netx/core/result.hpp"
#include "netx/core/sleep.hpp"
#include "netx/core/task.hpp"
#include "netx/core/when_any.hpp"
#include "netx/core/wrapped_task.hpp"

#include "netx/net/address.hpp"
#include "netx/net/buffer.hpp"
#include "netx/net/endian.hpp"
#include "netx/net/lock_free_queue.hpp"
#include "netx/net/scheduler.hpp"
#include "netx/net/server.hpp"
#include "netx/net/socket.hpp"
#include "netx/net/stream.hpp"

#include "netx/http/parser.hpp"
#include "netx/http/radix_tree.hpp"
#include "netx/http/request.hpp"
#include "netx/http/response.hpp"
#include "netx/http/router.hpp"
#include "netx/http/sender.hpp"
#include "netx/http/server.hpp"
#include "netx/http/session.hpp"

#include "netx/websocket/connection.hpp"
#include "netx/websocket/frame.hpp"
#include "netx/websocket/handshake.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("所有公开头文件都能独立编过", "[compile]") {
    // 断言只是为了让这个用例有个身份；真正被检查的是编译本身。
    SUCCEED("included every public header");
}
