add_library(netx INTERFACE)

target_include_directories(netx
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(netx PROPERTIES DEBUG_POSTFIX "_d")
endif()


if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fcoroutines)
endif()