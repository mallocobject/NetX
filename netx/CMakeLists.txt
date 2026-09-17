# netx 是 header-only，这里只声明用法要求（usage requirements），
# 供宿主通过 add_subdirectory / FetchContent 直接链接。
add_library(netx INTERFACE)

# 带命名空间的别名：宿主写 target_link_libraries(app PRIVATE netx::netx)，
# 这样即使以后 netx 变成有导出的包，写法也不用改。
add_library(netx::netx ALIAS netx)

# C++23 挂在 target 上，而不是在根 CMakeLists 里 set(CMAKE_CXX_STANDARD)：
# 被 FetchContent 拉进宿主项目时，全局设置会连带改掉对方其它 target 的标准。
target_compile_features(netx INTERFACE cxx_std_23)

target_include_directories(netx
    INTERFACE
        # 头文件在仓库根下（netx/... elog/...），所以包含的是上一层
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(netx INTERFACE -fcoroutines)
endif()

# netx 的头文件里直接 #include "elog/logger.hpp"，所以必须把 elog 的
# include 目录传递出去。少了这一句，树内构建会因为仓库根下正好有 elog/
# 而侥幸通过，被 FetchContent 拉走时就找不到头文件了。
if(NOT TARGET elog)
    message(FATAL_ERROR
        "netx needs the elog target; add_subdirectory(elog) or "
        "FetchContent_Declare(elog GIT_REPOSITORY "
        "https://github.com/mallocobject/Elog.git) first")
endif()
target_link_libraries(netx INTERFACE elog)

# WebSocket 握手要 SHA1 + Base64
find_package(OpenSSL REQUIRED)
target_link_libraries(netx INTERFACE OpenSSL::SSL OpenSSL::Crypto)
