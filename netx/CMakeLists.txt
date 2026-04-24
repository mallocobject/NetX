cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(netx INTERFACE)

target_include_directories(netx
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(netx PROPERTIES DEBUG_POSTFIX "_d")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(netx INTERFACE -fcoroutines)
endif()

find_package(OpenSSL REQUIRED)
target_link_libraries(netx INTERFACE OpenSSL::SSL OpenSSL::Crypto)