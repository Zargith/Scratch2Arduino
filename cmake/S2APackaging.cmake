# Packages made with CPack (`cpack` in the build folder), attached to each release:
#   - Linux: .deb (Debian, Ubuntu, Mint) and .rpm (Fedora, openSUSE), installed in /usr, with the
#     system's WebKitGTK as a dependency and a udev rule for the boards' serial ports;
#   - macOS: a .dmg holding Scratch2Arduino.app and a link to Applications, signed ad hoc (no
#     Apple certificate: the first opening goes through "Open Anyway").
# Windows has its own installer (installer/windows, Inno Setup).
if(WIN32 OR NOT S2A_BUILD_WEBVIEW)
    return()
endif()

set(CPACK_PACKAGE_NAME "scratch2arduino")
set(CPACK_PACKAGE_VENDOR "Scratch2Arduino")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Program Arduino boards with Scratch-style blocks")
set(CPACK_PACKAGE_DESCRIPTION
    "Scratch2Arduino lets beginners program Arduino and ESP32 boards with Scratch-style blocks: upload, live mode, virtual board, wiring diagrams, Python and JavaScript.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Zargith/Scratch2Arduino")
set(CPACK_PACKAGE_CONTACT "https://github.com/Zargith/Scratch2Arduino/issues")
set(CPACK_STRIP_FILES ON)

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Scratch2Arduino")
    set(CPACK_DMG_FORMAT "UDZO")
    if(CMAKE_OSX_ARCHITECTURES)
        string(REPLACE ";" "-" _s2a_arch "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(_s2a_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(CPACK_PACKAGE_FILE_NAME "Scratch2Arduino-${PROJECT_VERSION}-macos-${_s2a_arch}")
    # Ad hoc signature of the whole bundle (the files added after linking break the linker's one):
    # macOS then says "unidentified developer" (Open Anyway) instead of "damaged".
    set(CPACK_PRE_BUILD_SCRIPTS "${PROJECT_SOURCE_DIR}/cmake/S2ASignBundle.cmake")
else()
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")

    # Debian / Ubuntu: the libraries' packages are found by dpkg-shlibdeps; a file dialog program is
    # needed to open and save projects.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "zenity | kdialog")
    set(CPACK_DEBIAN_PACKAGE_SECTION "electronics")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${PROJECT_SOURCE_DIR}/resources/linux/postinst;${PROJECT_SOURCE_DIR}/resources/linux/postrm")

    # Fedora / openSUSE: library dependencies found by rpmbuild, plus the dialog program.
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    set(CPACK_RPM_PACKAGE_REQUIRES "zenity")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${PROJECT_SOURCE_DIR}/resources/linux/postinst")
    set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${PROJECT_SOURCE_DIR}/resources/linux/postrm")
    # Folders that belong to the system, not to the package.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/lib/udev /usr/lib/udev/rules.d /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
        /usr/libexec)
endif()

include(CPack)
