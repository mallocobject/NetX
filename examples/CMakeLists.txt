cmake_minimum_required(VERSION 3.22)
project(netx_examples)

# 不指定就选 Release。CMAKE_BUILD_TYPE 留空时 CMake 一个优化选项都不加，
# 拿到的是事实上的 -O0：这个坑很隐蔽，尤其 examples 这种先编先跑的目标。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "Cmake build mode: ${CMAKE_BUILD_TYPE}")

# elog 用了 C++23 的 <print>，GCC 要到 14 才提供。不在这里报错的话，
# 用户看到的是一句没头没尾的 "print: No such file or directory"。
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR
        "NetX needs GCC 14 or newer (found ${CMAKE_CXX_COMPILER_VERSION}); "
        "C++23 <print> is not available before 14. "
        "Configure with -DCMAKE_CXX_COMPILER=g++-14")
endif()


# 支持两种构建方式：作为根项目的子目录，或独立构建（cmake -S examples）。
if(NOT TARGET elog)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../elog
                     ${CMAKE_BINARY_DIR}/elog)
endif()
if(NOT TARGET netx)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../netx
                     ${CMAKE_BINARY_DIR}/netx)
endif()

# HTTP server：路由 / 路径参数 / 静态文件
add_executable(netx_http http_server.cpp)
# WebSocket echo：浏览器聊天页 + /ws 回显
add_executable(netx_ws ws_echo.cpp)
# 原始 TCP 层：直接基于 net::details::Server 的 echo 服务
add_executable(netx_tcp_echo tcp_echo.cpp)

# 静态文件与聊天页的目录，编译期传进去
target_compile_definitions(netx_http PRIVATE
    NETX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public")
target_compile_definitions(netx_ws PRIVATE
    NETX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public")

target_link_libraries(netx_http PRIVATE netx elog)
target_link_libraries(netx_ws PRIVATE netx elog)
target_link_libraries(netx_tcp_echo PRIVATE netx elog)

# 例子是头文件被真正实例化的地方之一，警告开着当静态防线
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target netx_http netx_ws netx_tcp_echo)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endforeach()
endif()
