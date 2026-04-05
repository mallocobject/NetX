cmake_minimum_required(VERSION 3.25)
project(netx)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS "Cmake build mode: ${CMAKE_BUILD_TYPE}")

add_subdirectory(elog)
add_subdirectory(netx)
add_subdirectory(test)

