project(test)

find_package(Catch2 3 REQUIRED)

# 启用 CTest 后 `test` 这个目标名被保留，配置时会直接报
#   The target name "test" is reserved when CTest testing is enabled.
# 所以可执行文件不能叫 test，这里改名为 netx_test。
add_executable(netx_test
    # hello.cpp
    result.cpp
    expected.cpp
    event_loop.cpp
    task.cpp
    async_main.cpp
    cancel.cpp
    sleep.cpp
    when_any.cpp
    elog.cpp
    address.cpp
    endian.cpp
    socket.cpp
    buffer.cpp
    lock_free_queue.cpp
    stream.cpp
    scheduler.cpp
    server.cpp
    http.cpp
    compile_all.cpp
    websocket.cpp)

target_link_libraries(netx_test PRIVATE netx elog Catch2::Catch2WithMain)

# 测试是唯一大规模实例化这些头文件的地方，把警告打开等于给头文件加了一道
# 静态防线：-Wall 里含 -Wreorder，能挡住"成员按声明顺序初始化、初始化列表
# 里写的顺序不算数"这类 UB（构造函数读未初始化成员）。
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(netx_test PRIVATE -Wall -Wextra)
endif()

include(Catch)
catch_discover_tests(netx_test)