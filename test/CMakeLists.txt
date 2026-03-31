cmake_minimum_required(VERSION 3.25)
project(test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test1 json.cpp)
# add_executable(test2 rpc_server_mt.cpp)
# add_executable(test3 rpc_client_v3_copy.cpp)

target_link_libraries(test1 PRIVATE netx elog)
# target_link_libraries(test2 PRIVATE netx elog)
# target_link_libraries(test3 PRIVATE netx elog)

target_compile_definitions(test1 PRIVATE 
    LYNX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
# target_compile_definitions(test2 PRIVATE 
#     LYNX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
# )
# target_compile_definitions(test3 PRIVATE 
#     LYNX_WEB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
# )