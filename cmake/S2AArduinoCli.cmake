# Bundled arduino-cli: downloaded at configure time (pinned version and SHA-256), copied next to
# every application in the build tree and installed with them, so Scratch2Arduino never depends
# on an arduino-cli installed by the user.
#
#   s2a_bundle_arduino_cli(<target>)   copies arduino-cli next to <target> and installs it
#
# Lookup order at runtime: see s2a::toolchain::locateArduinoCli() (core/src/arduino_cli.cpp).
option(S2A_BUNDLE_ARDUINO_CLI "Download arduino-cli and ship it with the applications" ON)

set(S2A_ARDUINO_CLI_VERSION "1.5.1")
set(S2A_ARDUINO_CLI_EXECUTABLE "")

if(S2A_BUNDLE_ARDUINO_CLI)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _s2a_cpu)
    if(WIN32)
        set(_s2a_asset "Windows_64bit.zip")
        set(_s2a_hash "fabe42e0eb04d00e776a66178299ff95a46c623dbc260f997e58fd514853dd40")
    elseif(APPLE AND _s2a_cpu MATCHES "arm64|aarch64")
        set(_s2a_asset "macOS_ARM64.tar.gz")
        set(_s2a_hash "cb952e8c1621c95ef5f1d17831c945e3d0ec5973f89c557a7ec8feb9c4f7d4c9")
    elseif(APPLE)
        set(_s2a_asset "macOS_64bit.tar.gz")
        set(_s2a_hash "c982e940027996bea9901050e95fae99c59c1dcfee54beedecaf28141e7bf2e7")
    elseif(_s2a_cpu MATCHES "aarch64|arm64")
        set(_s2a_asset "Linux_ARM64.tar.gz")
        set(_s2a_hash "1e69e077479f300614d4551334e0a33f08ee40b04315d83b8e7e0e94f0d0ee62")
    elseif(_s2a_cpu MATCHES "x86_64|amd64")
        set(_s2a_asset "Linux_64bit.tar.gz")
        set(_s2a_hash "28a8e119c498a25607821c36cb2dc49e8463941b261a0d99091baa7bc692dd2b")
    else()
        message(WARNING "No bundled arduino-cli for ${CMAKE_HOST_SYSTEM_PROCESSOR}: the arduino-cli found in the PATH will be used.")
    endif()

    if(_s2a_asset)
        include(FetchContent)
        FetchContent_Declare(arduino_cli
            URL "https://github.com/arduino/arduino-cli/releases/download/v${S2A_ARDUINO_CLI_VERSION}/arduino-cli_${S2A_ARDUINO_CLI_VERSION}_${_s2a_asset}"
            URL_HASH SHA256=${_s2a_hash}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(arduino_cli)
        set(S2A_ARDUINO_CLI_EXECUTABLE "${arduino_cli_SOURCE_DIR}/arduino-cli${CMAKE_EXECUTABLE_SUFFIX}")
        if(NOT EXISTS "${S2A_ARDUINO_CLI_EXECUTABLE}")
            message(FATAL_ERROR "arduino-cli not found in the downloaded archive: ${S2A_ARDUINO_CLI_EXECUTABLE}")
        endif()
        message(STATUS "Bundled arduino-cli ${S2A_ARDUINO_CLI_VERSION}: ${S2A_ARDUINO_CLI_EXECUTABLE}")
    endif()
endif()

function(s2a_bundle_arduino_cli target)
    if(NOT S2A_ARDUINO_CLI_EXECUTABLE)
        return()
    endif()
    get_filename_component(name "${S2A_ARDUINO_CLI_EXECUTABLE}" NAME)
    # Build tree: next to the executable (inside Contents/MacOS for a macOS bundle).
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${S2A_ARDUINO_CLI_EXECUTABLE}" "$<TARGET_FILE_DIR:${target}>/${name}"
        VERBATIM)
    # Install tree.
    get_target_property(is_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND is_bundle)
        install(PROGRAMS "${S2A_ARDUINO_CLI_EXECUTABLE}" DESTINATION "$<TARGET_FILE_NAME:${target}>.app/Contents/MacOS")
    elseif(WIN32)
        install(PROGRAMS "${S2A_ARDUINO_CLI_EXECUTABLE}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        # Not in bin/: it must not shadow an arduino-cli installed by the user.
        install(PROGRAMS "${S2A_ARDUINO_CLI_EXECUTABLE}" DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/scratch2arduino)
    endif()
endfunction()
