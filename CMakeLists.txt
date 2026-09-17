cmake_minimum_required(VERSION 3.22)
project(netx)

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

include(CTest)
include(FetchContent)

FetchContent_Declare(
    elog
    GIT_REPOSITORY https://github.com/mallocobject/Elog
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(elog)

add_subdirectory(netx)
add_subdirectory(examples)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()