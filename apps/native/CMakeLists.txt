# Variant B - pure C++ Qt 6 application.
find_package(Qt6 6.3 COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt 6 (Widgets) introuvable / not found.\n"
        "  - Linux : ./install.sh installe les dépendances / installs the dependencies\n"
        "  - macOS : brew install qt, puis/then -DCMAKE_PREFIX_PATH=$(brew --prefix qt)\n"
        "  - Windows : installer.exe, ou/or -DCMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64\n"
        "Sinon / otherwise: -DS2A_BUILD_NATIVE=OFF")
endif()

qt_standard_project_setup()

qt_add_executable(scratch2arduino-native WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/main_window.cpp
    src/main_window.hpp
)
target_link_libraries(scratch2arduino-native PRIVATE s2a::core Qt6::Widgets)
s2a_set_warnings(scratch2arduino-native)

set_target_properties(scratch2arduino-native PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Scratch2Arduino (native)"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.zargith.scratch2arduino.native"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

install(TARGETS scratch2arduino-native
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .  # macOS: the prefix is the Applications folder (required by Qt deploy)
)

# Windows and macOS: copy the Qt runtime next to the executable / into the bundle.
# On Linux the distribution's Qt packages are used.
if((WIN32 OR APPLE) AND Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    qt_generate_deploy_app_script(
        TARGET scratch2arduino-native
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT "${deploy_script}")
endif()

s2a_install_desktop_entry(scratch2arduino-native NAME "Scratch2Arduino (native)" VARIANT "native")
