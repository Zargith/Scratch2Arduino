# Scratch2Arduino application - native C++ host embedding the system webview
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
            "  - ./Scratch2Arduino_installer.sh installe les dépendances / installs the dependencies\n"
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

# Application icon: resource of the .exe on Windows, Resources/ of the bundle on macOS.
set(S2A_ICON_DIR "${PROJECT_SOURCE_DIR}/resources/icons")
set(_s2a_icon_sources)
if(WIN32)
    configure_file(src/scratch2arduino.rc.in "${CMAKE_CURRENT_BINARY_DIR}/scratch2arduino.rc" @ONLY)
    list(APPEND _s2a_icon_sources "${CMAKE_CURRENT_BINARY_DIR}/scratch2arduino.rc")
elseif(APPLE)
    set(_s2a_icns "${S2A_ICON_DIR}/scratch2arduino.icns")
    set_source_files_properties("${_s2a_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND _s2a_icon_sources "${_s2a_icns}")
endif()

add_executable(scratch2arduino WIN32 MACOSX_BUNDLE
    src/bridge.cpp
    src/main.cpp
    src/welcome_page.cpp
    ${_s2a_icon_sources}
)
target_include_directories(scratch2arduino PRIVATE src)
target_include_directories(scratch2arduino SYSTEM PRIVATE "${portable_file_dialogs_SOURCE_DIR}")
target_link_libraries(scratch2arduino PRIVATE s2a::core webview::core nlohmann_json::nlohmann_json Threads::Threads)
if(WIN32)
    target_compile_definitions(scratch2arduino PRIVATE NOMINMAX)
    # CommandLineToArgvW: the project dropped on the icon.
    target_link_libraries(scratch2arduino PRIVATE shell32)
endif()
s2a_set_warnings(scratch2arduino)

set_target_properties(scratch2arduino PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Scratch2Arduino"
    MACOSX_BUNDLE_ICON_FILE scratch2arduino.icns
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.zargith.scratch2arduino"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

install(TARGETS scratch2arduino
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .  # macOS: the prefix is the Applications folder
)
s2a_bundle_arduino_cli(scratch2arduino)
s2a_bundle_frontend(scratch2arduino)
s2a_install_desktop_entry(scratch2arduino NAME "Scratch2Arduino")
