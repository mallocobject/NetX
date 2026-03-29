add_library(elog INTERFACE)

target_include_directories(elog 
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(elog PROPERTIES DEBUG_POSTFIX "_d")
endif()