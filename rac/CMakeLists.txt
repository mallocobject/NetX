# set(LIB_SOURCES)

# file(GLOB LIB_SOURCES CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(rac INTERFACE)

target_include_directories(rac
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(rac PROPERTIES DEBUG_POSTFIX "_d")
endif()


if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fcoroutines)
endif()