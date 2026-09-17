project(test)

find_package(Catch2 3 REQUIRED)

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
    address.cpp
    endian.cpp
    socket.cpp
    buffer.cpp
    mpsc_queue.cpp
    stream.cpp
    scheduler.cpp
    server.cpp
    http.cpp
    compile_all.cpp
    websocket.cpp
    sender.cpp)

target_link_libraries(netx_test PRIVATE netx elog Catch2::Catch2WithMain)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(netx_test PRIVATE -Wall -Wextra)
endif()

include(Catch)
catch_discover_tests(netx_test)