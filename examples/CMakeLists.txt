cmake_minimum_required(VERSION 3.22)
project(netx_examples)

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

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR
        "NetX needs GCC 14 or newer (found ${CMAKE_CXX_COMPILER_VERSION}); "
        "C++23 <print> is not available before 14. "
        "Configure with -DCMAKE_CXX_COMPILER=g++-14")
endif()


include(FetchContent)

# ---- 依赖：Elog（从 GitHub 拉取）----
if(NOT TARGET elog)
    FetchContent_Declare(
        elog
        GIT_REPOSITORY https://github.com/mallocobject/Elog.git
        GIT_TAG        main       
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(elog)
endif()

# ---- 依赖：NetX ----
# 若 examples 是被父工程 add_subdirectory 进来的，用本地源码；
# 若是独立配置，则需要从远端拉取（把 <NETX_REPO_URL> 换成实际地址）。
if(NOT TARGET netx)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../netx/CMakeLists.txt)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../netx
                         ${CMAKE_BINARY_DIR}/netx)
    else()
        FetchContent_Declare(
            netx
            GIT_REPOSITORY <NETX_REPO_URL>
            GIT_TAG        main
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(netx)
    endif()
endif()

add_executable(netx_http http_server.cpp)
add_executable(netx_ws   ws_echo.cpp)
add_executable(netx_tcp_echo tcp_echo.cpp)

target_compile_definitions(netx_http PRIVATE
    NETX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public")
target_compile_definitions(netx_ws PRIVATE
    NETX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public")

target_link_libraries(netx_http      PRIVATE netx elog)
target_link_libraries(netx_ws        PRIVATE netx elog)
target_link_libraries(netx_tcp_echo  PRIVATE netx elog)


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target netx_http netx_ws netx_tcp_echo)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endforeach()
endif()