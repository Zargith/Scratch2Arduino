# s2a_core - shared C++ library used by both variants.
include(S2ADependencies)

configure_file(include/s2a/version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/s2a/version.hpp" @ONLY)

add_library(s2a_core STATIC
    src/arduino_cli.cpp
    src/blocks.cpp
    src/build.cpp
    src/codegen.cpp
    src/boards.cpp
    src/diagnostics.cpp
    src/archive.cpp
    src/i18n.cpp
    src/process.cpp
    src/project.cpp
    src/resources.cpp
    src/result.cpp
)
add_library(s2a::core ALIAS s2a_core)

target_include_directories(s2a_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)
target_compile_features(s2a_core PUBLIC cxx_std_17)
target_link_libraries(s2a_core PRIVATE nlohmann_json::nlohmann_json s2a_miniz reproc++)
s2a_set_warnings(s2a_core)

# Data compiled into the library: translations, board descriptions, block catalog.
file(GLOB S2A_CORE_RESOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/resources/i18n/*.json"
    "${PROJECT_SOURCE_DIR}/resources/boards/*.json"
    "${PROJECT_SOURCE_DIR}/blocks/catalog/*.json"
)
s2a_embed_resources(s2a_core BASE_DIR "${PROJECT_SOURCE_DIR}" FILES ${S2A_CORE_RESOURCES})

if(S2A_BUILD_TESTS)
    add_subdirectory(tests)
endif()
