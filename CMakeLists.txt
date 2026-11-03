cmake_minimum_required(VERSION 3.25)

project(Scratch2Arduino
    VERSION 0.1.0
    DESCRIPTION "Program Arduino boards with Scratch-style blocks"
    HOMEPAGE_URL "https://github.com/Zargith/Scratch2Arduino"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(S2A_BUILD_WEBVIEW "Build variant A (C++ host + system webview)" ON)
option(S2A_BUILD_NATIVE "Build variant B (Qt 6 native application)" ON)
option(S2A_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(S2A_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(S2ACompilerWarnings)
include(S2ADesktopEntry)
include(S2AEmbedResources)

# Sources are UTF-8 (French strings).
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

if(NOT S2A_BUILD_WEBVIEW AND NOT S2A_BUILD_NATIVE)
    message(WARNING "Both variants are disabled: only the core library will be built.")
endif()

if(S2A_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)

if(S2A_BUILD_WEBVIEW)
    add_subdirectory(apps/webview)
endif()

if(S2A_BUILD_NATIVE)
    add_subdirectory(apps/native)
endif()

# Linux desktop integration (application icon).
if(UNIX AND NOT APPLE AND (S2A_BUILD_WEBVIEW OR S2A_BUILD_NATIVE))
    install(FILES resources/icons/scratch2arduino.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

# `cmake -P <build>/cmake_uninstall.cmake` (used by `make uninstall` and `install.sh --uninstall`).
configure_file(cmake/S2AUninstall.cmake.in "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake" @ONLY)
add_custom_target(uninstall COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake")

message(STATUS "Scratch2Arduino ${PROJECT_VERSION}: webview=${S2A_BUILD_WEBVIEW} native=${S2A_BUILD_NATIVE} tests=${S2A_BUILD_TESTS}")
