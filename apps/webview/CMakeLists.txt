# Variant A - native C++ host embedding the system webview
# (WebKitGTK on Linux, WKWebView on macOS, WebView2 on Windows).
include(FetchContent)

# Linux: check WebKitGTK up front for a readable error (webview's own error is cryptic).
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_search_module(S2A_WEBKITGTK QUIET webkit2gtk-4.1 webkitgtk-6.0 webkit2gtk-4.0)
    endif()
    if(NOT S2A_WEBKITGTK_FOUND)
        message(FATAL_ERROR
            "WebKitGTK (et/and pkg-config) introuvable / not found.\n"
            "  - ./install.sh installe les dépendances / installs the dependencies\n"
            "  - Debian/Ubuntu : sudo apt install pkg-config libwebkit2gtk-4.1-dev\n"
            "Sinon / otherwise: -DS2A_BUILD_WEBVIEW=OFF")
    endif()
endif()

set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_CHECKS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_CLANG_FORMAT OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_CLANG_TIDY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_STATIC_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(webview
    URL https://github.com/webview/webview/archive/refs/tags/0.12.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
FetchContent_MakeAvailable(webview)

# portable-file-dialogs - native open/save dialogs (WTFPL, header only). Linux uses zenity or kdialog.
FetchContent_Declare(portable_file_dialogs
    URL https://github.com/samhocevar/portable-file-dialogs/archive/c12ea8c9a727f5320a2b4570aee863bbede2a204.zip
    URL_HASH SHA256=49271b213d08588646e34b8020019d43167fb98ee66e00d41aa2365e926d4320
    SOURCE_SUBDIR no-cmake  # header only: do not run its CMakeLists (examples)
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(portable_file_dialogs)

include(S2AFrontend)
find_package(Threads REQUIRED)

add_executable(scratch2arduino-webview WIN32 MACOSX_BUNDLE
    src/bridge.cpp
    src/main.cpp
    src/welcome_page.cpp
)
target_include_directories(scratch2arduino-webview PRIVATE src)
target_include_directories(scratch2arduino-webview SYSTEM PRIVATE "${portable_file_dialogs_SOURCE_DIR}")
target_link_libraries(scratch2arduino-webview PRIVATE s2a::core webview::core nlohmann_json::nlohmann_json Threads::Threads)
s2a_set_warnings(scratch2arduino-webview)

set_target_properties(scratch2arduino-webview PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Scratch2Arduino (webview)"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.zargith.scratch2arduino.webview"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

install(TARGETS scratch2arduino-webview
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .  # macOS: the prefix is the Applications folder (required by Qt deploy)
)
s2a_bundle_arduino_cli(scratch2arduino-webview)
s2a_bundle_frontend(scratch2arduino-webview)
s2a_install_desktop_entry(scratch2arduino-webview NAME "Scratch2Arduino (webview)" VARIANT "webview")
