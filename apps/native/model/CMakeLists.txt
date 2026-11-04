# Block canvas model of variant B: geometry and drag-and-drop edits, without Qt so that it is
# unit-tested even when Qt is not installed (core-only builds).
add_library(s2a_native_model STATIC
    editing.cpp
    editing.hpp
    layout.cpp
    layout.hpp
)
target_include_directories(s2a_native_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(s2a_native_model PUBLIC s2a::core)
s2a_set_warnings(s2a_native_model)

if(S2A_BUILD_TESTS)
    add_subdirectory(tests)
endif()
