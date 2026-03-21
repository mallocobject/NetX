cmake_minimum_required(VERSION 3.25)
project(test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test when_any.cpp)

target_link_libraries(test PRIVATE rac elog)

target_compile_definitions(test PRIVATE 
    LYNX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)