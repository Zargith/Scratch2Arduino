# s2a_bridge - the JavaScript <-> C++ calls of the application, without the webview (audit #197):
# the application gives it the window, the tests a fake one.
include(FetchContent)

# portable-file-dialogs - native open/save dialogs (WTFPL, header only). Linux uses zenity or kdialog.
FetchContent_Declare(portable_file_dialogs
    URL https://github.com/samhocevar/portable-file-dialogs/archive/c12ea8c9a727f5320a2b4570aee863bbede2a204.zip
    URL_HASH SHA256=49271b213d08588646e34b8020019d43167fb98ee66e00d41aa2365e926d4320
    SOURCE_SUBDIR no-cmake  # header only: do not run its CMakeLists (examples)
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(portable_file_dialogs)
find_package(Threads REQUIRED)

add_library(s2a_bridge STATIC bridge.cpp bridge_helpers.cpp)
target_include_directories(s2a_bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(s2a_bridge SYSTEM PUBLIC "${portable_file_dialogs_SOURCE_DIR}")
target_link_libraries(s2a_bridge PUBLIC s2a::core nlohmann_json::nlohmann_json Threads::Threads)
if(WIN32)
    target_compile_definitions(s2a_bridge PUBLIC NOMINMAX)
    # URLDownloadToFileW, COM: the update; ShellExecute: links and the installer.
    target_link_libraries(s2a_bridge PUBLIC shell32 urlmon ole32)
endif()
s2a_set_warnings(s2a_bridge)

if(S2A_BUILD_TESTS)
    add_subdirectory(tests)
endif()
