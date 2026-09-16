cmake_minimum_required(VERSION 3.22)
project(netx)

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


include(CTest)

add_subdirectory(netx)
add_subdirectory(elog)
add_subdirectory(examples)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()

