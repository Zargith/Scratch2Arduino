find_package(GTest 1.12 CONFIG QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(s2a_core_tests
    test_arduino_cli.cpp
    test_blocks.cpp
    test_boards.cpp
    test_build.cpp
    test_codegen.cpp
    test_diagnostics.cpp
    test_i18n.cpp
    test_process.cpp
    test_project.cpp
    test_resources.cpp
    test_result.cpp
    test_version.cpp
)
target_link_libraries(s2a_core_tests PRIVATE s2a::core s2a_miniz GTest::gtest_main)
s2a_set_warnings(s2a_core_tests)

# Program driven by test_process.cpp.
add_executable(s2a_process_helper process_helper.cpp)
if(WIN32)
    target_link_libraries(s2a_process_helper PRIVATE shell32)
endif()
add_dependencies(s2a_core_tests s2a_process_helper)
target_compile_definitions(s2a_core_tests PRIVATE S2A_PROCESS_HELPER="$<TARGET_FILE:s2a_process_helper>")
if(S2A_ARDUINO_CLI_EXECUTABLE)
    target_compile_definitions(s2a_core_tests PRIVATE
        S2A_BUNDLED_ARDUINO_CLI="${S2A_ARDUINO_CLI_EXECUTABLE}"
        S2A_BUNDLED_ARDUINO_CLI_VERSION="${S2A_ARDUINO_CLI_VERSION}")
endif()

include(GoogleTest)
gtest_discover_tests(s2a_core_tests)
