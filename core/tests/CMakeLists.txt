# GoogleTest is found (or downloaded) by the top-level CMakeLists.txt, for every test target.
add_executable(s2a_core_tests
    test_arduino_cli.cpp
    test_board_packages.cpp
    test_blocks.cpp
    test_boards.cpp
    test_build.cpp
    test_codegen.cpp
    test_integrity.cpp
    test_diagnostics.cpp
    test_i18n.cpp
    test_live.cpp
    test_simulator.cpp
    test_network.cpp
    test_wiring.cpp
    test_process.cpp
    test_project.cpp
    test_resources.cpp
    test_result.cpp
    test_rosetta.cpp
    test_script.cpp
    test_serial.cpp
    test_usercode.cpp
    test_convert.cpp
    test_userdata.cpp
    test_validator.cpp
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
target_compile_definitions(s2a_core_tests PRIVATE S2A_PROCESS_HELPER="$<TARGET_FILE:s2a_process_helper>"
    S2A_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples/projects"
    S2A_UDEV_RULES="${PROJECT_SOURCE_DIR}/resources/linux/60-scratch2arduino.rules")
if(S2A_ARDUINO_CLI_EXECUTABLE)
    target_compile_definitions(s2a_core_tests PRIVATE
        S2A_BUNDLED_ARDUINO_CLI="${S2A_ARDUINO_CLI_EXECUTABLE}"
        S2A_BUNDLED_ARDUINO_CLI_VERSION="${S2A_ARDUINO_CLI_VERSION}")
endif()

include(GoogleTest)
gtest_discover_tests(s2a_core_tests)
