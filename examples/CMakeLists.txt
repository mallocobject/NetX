cmake_minimum_required(VERSION 3.22)
project(netx_examples)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS "Cmake build mode: ${CMAKE_BUILD_TYPE}")

# 支持两种构建方式:作为根项目的子目录,或独立构建(cmake -S examples)。
if(NOT TARGET elog)
    add_subdirectory(../elog ${CMAKE_BINARY_DIR}/elog)
endif()
if(NOT TARGET netx)
    add_subdirectory(../netx ${CMAKE_BINARY_DIR}/netx)
endif()

# HTTP server:路由 / 路径参数 / 静态文件
add_executable(netx_http http_server.cpp)
# WebSocket echo:浏览器聊天页 + /ws 回显
add_executable(netx_ws ws_echo.cpp)
# 原始 TCP 层:直接基于 net::details::Server 的 echo 服务
add_executable(netx_tcp_echo tcp_echo.cpp)

target_compile_definitions(netx_http PRIVATE
    NETX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public"
)
target_compile_definitions(netx_ws PRIVATE
    NETX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public"
)

target_link_libraries(netx_http PRIVATE elog netx)
target_link_libraries(netx_ws PRIVATE elog netx)
target_link_libraries(netx_tcp_echo PRIVATE elog netx)
