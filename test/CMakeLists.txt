project(test)

find_package(Catch2 3 REQUIRED)

# 启用 CTest 后 `test` 这个目标名被保留，配置时会直接报
#   The target name "test" is reserved when CTest testing is enabled.
# 所以可执行文件不能叫 test，这里改名为 netx_test。
add_executable(netx_test
    result.cpp
    expected.cpp
    event_loop.cpp
    task.cpp
    async_main.cpp
    cancel.cpp
    sleep.cpp
    when_any.cpp)

target_link_libraries(netx_test PRIVATE netx Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(netx_test)