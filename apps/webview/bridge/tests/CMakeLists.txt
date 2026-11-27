add_executable(s2a_bridge_tests test_bridge.cpp)
target_link_libraries(s2a_bridge_tests PRIVATE s2a_bridge GTest::gtest_main)
target_compile_definitions(s2a_bridge_tests PRIVATE
    S2A_FRONTEND_BRIDGE_TS="${PROJECT_SOURCE_DIR}/frontend/src/bridge.ts"
    S2A_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples/projects")
s2a_set_warnings(s2a_bridge_tests)

include(GoogleTest)
gtest_discover_tests(s2a_bridge_tests)
