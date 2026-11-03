# s2a_core - shared C++ library used by both variants.
configure_file(include/s2a/version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/s2a/version.hpp" @ONLY)

add_library(s2a_core STATIC
    src/i18n.cpp
)
add_library(s2a::core ALIAS s2a_core)

target_include_directories(s2a_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)
target_compile_features(s2a_core PUBLIC cxx_std_17)
s2a_set_warnings(s2a_core)

if(S2A_BUILD_TESTS)
    add_subdirectory(tests)
endif()
