cmake_minimum_required(VERSION 3.25)

project(Scratch2Arduino
    VERSION 1.0.0
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

option(S2A_BUILD_WEBVIEW "Build the application (C++ host + system webview)" ON)
option(S2A_BUILD_CLI "Build the s2a-cli command-line tool" ON)
option(S2A_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(S2A_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(S2A_INSTALL_UDEV_RULES "Linux: install the udev rule giving the logged-in user the boards' serial ports (packages)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(S2ACompilerWarnings)
include(S2ADesktopEntry)
include(S2AEmbedResources)
include(S2AArduinoCli)

# Sources are UTF-8 (French strings).
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

if(S2A_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)

if(S2A_BUILD_CLI)
    add_subdirectory(tools/s2a-cli)
endif()

if(S2A_BUILD_WEBVIEW)
    add_subdirectory(apps/webview)
endif()

# Linux desktop integration (application icon).
if(UNIX AND NOT APPLE AND S2A_BUILD_WEBVIEW)
    foreach(size 16 32 48 64 128 256)
        install(FILES resources/icons/scratch2arduino-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME scratch2arduino.png)
    endforeach()
    install(FILES resources/icons/scratch2arduino.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
    if(S2A_INSTALL_UDEV_RULES)
        install(FILES resources/linux/60-scratch2arduino.rules DESTINATION lib/udev/rules.d)
    endif()
endif()

# `cmake -P <build>/cmake_uninstall.cmake` (used by `make uninstall` and `Scratch2Arduino_installer.sh --uninstall`).
configure_file(cmake/S2AUninstall.cmake.in "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake" @ONLY)
add_custom_target(uninstall COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake")

# .deb / .rpm (Linux) and .dmg (macOS): `cpack` in the build folder.
include(S2APackaging)

message(STATUS "Scratch2Arduino ${PROJECT_VERSION}: app=${S2A_BUILD_WEBVIEW} cli=${S2A_BUILD_CLI} tests=${S2A_BUILD_TESTS}")
