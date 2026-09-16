cmake_minimum_required(VERSION 3.22)
project(netx)

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


include(CTest)

add_subdirectory(netx)
add_subdirectory(elog)
add_subdirectory(examples)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()

