cmake_minimum_required(VERSION 3.22)
project(netx_gui)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS "Cmake build mode: ${CMAKE_BUILD_TYPE}")

add_subdirectory(../elog ${CMAKE_BINARY_DIR}/elog)
add_subdirectory(../netx ${CMAKE_BINARY_DIR}/netx)

include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

add_executable(netx_gui main.cpp)

target_compile_definitions(netx_gui PRIVATE
    NETX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public"
)

target_link_libraries(netx_gui PRIVATE
    ftxui::component
    ftxui::dom
    ftxui::screen
    elog
    netx
)