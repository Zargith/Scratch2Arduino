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
# windeployqt / macdeployqt are called directly rather than through qt_generate_deploy_app_script(),
# whose generated script breaks on paths containing spaces ("C:/Program Files/...", Qt 6.8).
if(WIN32 AND TARGET Qt6::windeployqt)
    install(CODE "
        execute_process(
            COMMAND \"$<TARGET_FILE:Qt6::windeployqt>\" --release --no-translations
                    --no-system-d3d-compiler --no-opengl-sw
                    \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:scratch2arduino-native>\"
            RESULT_VARIABLE s2a_deploy_result)
        if(NOT s2a_deploy_result EQUAL 0)
            message(FATAL_ERROR \"windeployqt failed: \${s2a_deploy_result}\")
        endif()
    ")
elseif(APPLE AND TARGET Qt6::macdeployqt)
    install(CODE "
        execute_process(
            COMMAND \"$<TARGET_FILE:Qt6::macdeployqt>\"
                    \"\${CMAKE_INSTALL_PREFIX}/scratch2arduino-native.app\" -always-overwrite
            RESULT_VARIABLE s2a_deploy_result)
        if(NOT s2a_deploy_result EQUAL 0)
            message(FATAL_ERROR \"macdeployqt failed: \${s2a_deploy_result}\")
        endif()
    ")
elseif(WIN32 OR APPLE)
    message(WARNING "Qt deployment tool not found: the installed application will need Qt in the PATH.")
endif()

s2a_install_desktop_entry(scratch2arduino-native NAME "Scratch2Arduino (native)" VARIANT "native")
