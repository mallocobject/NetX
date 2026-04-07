add_library(netx INTERFACE)
# set(SQLITE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/db/sqlite3.c)
# set(SHELL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/db/shell.c)

# add_library(netx STATIC ${SQLITE_SRC})

target_include_directories(netx
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(netx PROPERTIES DEBUG_POSTFIX "_d")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fcoroutines)
endif()

find_package(OpenSSL REQUIRED)
target_link_libraries(netx INTERFACE OpenSSL::SSL OpenSSL::Crypto)