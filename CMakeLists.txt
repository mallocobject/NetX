cmake_minimum_required(VERSION 3.22)
project(netx)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "Cmake build mode: ${CMAKE_BUILD_TYPE}")

include(CTest)

add_subdirectory(netx)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()

