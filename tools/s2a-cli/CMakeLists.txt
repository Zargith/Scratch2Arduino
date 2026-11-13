# s2a-cli: command-line access to the Scratch2Arduino toolchain (boards, ports, compile, upload).
# Used by CI and handy for teachers checking a classroom computer.
add_executable(s2a-cli main.cpp)
target_link_libraries(s2a-cli PRIVATE s2a::core)
if(WIN32)
    target_link_libraries(s2a-cli PRIVATE shell32)
endif()
s2a_set_warnings(s2a-cli)
if(APPLE AND S2A_BUILD_WEBVIEW)
    # macOS: inside the application bundle, beside the application's arduino-cli (the .dmg then
    # holds the application alone): Scratch2Arduino.app/Contents/MacOS/s2a-cli.
    install(TARGETS s2a-cli RUNTIME DESTINATION "Scratch2Arduino.app/Contents/MacOS")
    s2a_bundle_arduino_cli(s2a-cli NO_INSTALL)
else()
    install(TARGETS s2a-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    s2a_bundle_arduino_cli(s2a-cli)
endif()
