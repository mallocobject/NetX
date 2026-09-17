cmake_minimum_required(VERSION 3.22)
project(netx VERSION 0.1.0 LANGUAGES CXX)

# ---- 只在自己是顶层项目时才动全局设置 ----
#
# 被 FetchContent 拉进宿主项目时 PROJECT_IS_TOP_LEVEL 为假：这时再
# set(... CACHE ... FORCE) 会直接覆盖宿主选好的 CMAKE_BUILD_TYPE，属于越界。
if(PROJECT_IS_TOP_LEVEL)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                     Debug Release RelWithDebInfo MinSizeRel)
    endif()
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    message(STATUS "Cmake build mode: ${CMAKE_BUILD_TYPE}")
endif()

# ---- 这一条不分顶层与否 ----
#
# elog 用了 C++23 的 <print>，GCC 要到 14 才提供。编译器不够新就是编不过，
# 早点报比后面报一句没头没尾的 "print: No such file or directory" 清楚。
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR
        "NetX needs GCC 14 or newer (found ${CMAKE_CXX_COMPILER_VERSION}); "
        "C++23 <print> is not available before 14. "
        "Configure with -DCMAKE_CXX_COMPILER=g++-14")
endif()

# C++23 挂在 netx 这个 target 上（见 netx/CMakeLists.txt），不在这里
# set(CMAKE_CXX_STANDARD) —— 全局设置会连带改掉宿主项目其它 target 的标准。

include(FetchContent)

# 作为子项目引入时默认什么都不构建：宿主只想链接 netx，
# 不该被塞进一堆测试可执行文件和三个例子。
option(NETX_BUILD_TESTS "Build NetX's own tests" ${PROJECT_IS_TOP_LEVEL})
option(NETX_BUILD_EXAMPLES "Build NetX's examples" ${PROJECT_IS_TOP_LEVEL})

# elog：已经有同名 target 就不再拉。宿主可以自己提供一份（指向本地检出，
# 或换个 fork），这时以宿主为准。
if(NOT TARGET elog)
    FetchContent_Declare(
        elog
        GIT_REPOSITORY https://github.com/mallocobject/Elog.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(elog)
endif()

add_subdirectory(netx)

if(NETX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(NETX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
