add_executable(s2a_native_model_tests
    test_editing.cpp
    test_layout.cpp
)
target_link_libraries(s2a_native_model_tests PRIVATE s2a_native_model GTest::gtest_main)
s2a_set_warnings(s2a_native_model_tests)
gtest_discover_tests(s2a_native_model_tests)
