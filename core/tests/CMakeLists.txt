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
    test_i18n.cpp
    test_resources.cpp
    test_result.cpp
    test_version.cpp
)
target_link_libraries(s2a_core_tests PRIVATE s2a::core GTest::gtest_main)
s2a_set_warnings(s2a_core_tests)

include(GoogleTest)
gtest_discover_tests(s2a_core_tests)
