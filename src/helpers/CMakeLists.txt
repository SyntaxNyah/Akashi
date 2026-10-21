# akashi.helpers: small, generic building blocks for reading and checking
# operator and player input - numbers, ranges, words, durations, lengths.
# Commands, config settings and plugins all use these, so the same input
# means the same thing everywhere. Nothing here may include core, world or
# protocol headers - only the SDK is visible, so the compiler keeps the
# helpers generic. The objects ship inside the akashi_core library, so
# exported functions use the core export macro.
add_library(akashi_helpers STATIC
    include/akashi/helpers/checks.h
    include/akashi/helpers/parse.h
    include/akashi/helpers/result.h
    checks.cpp
    parse.cpp
)

target_include_directories(akashi_helpers PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(akashi_helpers PUBLIC akashi_sdk)
target_compile_definitions(akashi_helpers PRIVATE akashi_core_EXPORTS)
set_target_properties(akashi_helpers PROPERTIES POSITION_INDEPENDENT_CODE ON)
