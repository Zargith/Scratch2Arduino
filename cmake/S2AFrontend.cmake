# Block editor UI (frontend/, scratch-blocks + React), shipped as files next to the
# application: index.html (everything inlined) and media/ (scratch-blocks icons).
#
#   s2a_bundle_frontend(<target>)   copies the frontend next to <target> and installs it
#
# Where the frontend comes from, in order:
#   1. S2A_FRONTEND_DIST: a folder that already holds index.html (prebuilt);
#   2. pnpm found (Node.js installed): built from frontend/ during the build;
#   3. frontend/dist already in the source tree (the Windows installer ships it prebuilt);
#   4. otherwise the prebuilt archive published with the release of this version is downloaded,
#      so building from source does not require Node.js.
include_guard(GLOBAL)

set(S2A_FRONTEND_DIST "" CACHE PATH "Prebuilt frontend folder (with index.html); empty = build it or download it")
set(S2A_FRONTEND_URL
    "https://github.com/Zargith/Scratch2Arduino/releases/download/v${PROJECT_VERSION}/scratch2arduino-frontend-${PROJECT_VERSION}.zip"
    CACHE STRING "Prebuilt frontend archive used when Node.js is not installed")

set(_s2a_frontend_source "${PROJECT_SOURCE_DIR}/frontend")
set(_s2a_frontend_output "${CMAKE_BINARY_DIR}/frontend-dist")

if(S2A_FRONTEND_DIST)
    if(NOT EXISTS "${S2A_FRONTEND_DIST}/index.html")
        message(FATAL_ERROR "S2A_FRONTEND_DIST=${S2A_FRONTEND_DIST} does not contain index.html")
    endif()
    set(_s2a_frontend_output "${S2A_FRONTEND_DIST}")
    add_custom_target(s2a_frontend)
    message(STATUS "Frontend: prebuilt (${S2A_FRONTEND_DIST})")
else()
    find_program(S2A_PNPM NAMES pnpm pnpm.cmd)
    if(S2A_PNPM)
        # pnpm is a .cmd script on Windows: Visual Studio runs custom commands from a .bat file,
        # where calling a .cmd without "call" ends the script after the first command.
        if(WIN32)
            set(_s2a_pnpm cmd /c "${S2A_PNPM}")
        else()
            set(_s2a_pnpm "${S2A_PNPM}")
        endif()
        file(GLOB_RECURSE _s2a_frontend_inputs CONFIGURE_DEPENDS
            "${_s2a_frontend_source}/src/*"
            "${PROJECT_SOURCE_DIR}/blocks/catalog/*.json"
            "${PROJECT_SOURCE_DIR}/resources/boards/*.json")
        add_custom_command(
            OUTPUT "${_s2a_frontend_output}/index.html"
            COMMAND ${_s2a_pnpm} install --frozen-lockfile
            COMMAND ${_s2a_pnpm} exec vite build --outDir "${_s2a_frontend_output}" --emptyOutDir --logLevel warn
            WORKING_DIRECTORY "${_s2a_frontend_source}"
            DEPENDS ${_s2a_frontend_inputs}
                "${_s2a_frontend_source}/package.json"
                "${_s2a_frontend_source}/pnpm-lock.yaml"
                "${_s2a_frontend_source}/vite.config.ts"
                "${_s2a_frontend_source}/index.html"
            COMMENT "Building the block editor (frontend/) with pnpm"
            VERBATIM)
        add_custom_target(s2a_frontend DEPENDS "${_s2a_frontend_output}/index.html")
        message(STATUS "Frontend: built with ${S2A_PNPM}")
    elseif(EXISTS "${_s2a_frontend_source}/dist/index.html")
        set(_s2a_frontend_output "${_s2a_frontend_source}/dist")
        add_custom_target(s2a_frontend)
        message(STATUS "Frontend: prebuilt in the source tree (frontend/dist)")
    else()
        set(_s2a_archive "${CMAKE_BINARY_DIR}/scratch2arduino-frontend-${PROJECT_VERSION}.zip")
        if(NOT EXISTS "${_s2a_frontend_output}/index.html")
            file(DOWNLOAD "${S2A_FRONTEND_URL}" "${_s2a_archive}" STATUS _s2a_status TLS_VERIFY ON)
            list(GET _s2a_status 0 _s2a_code)
            if(NOT _s2a_code EQUAL 0)
                file(REMOVE "${_s2a_archive}")
                message(FATAL_ERROR
                    "Éditeur de blocs : Node.js (pnpm) introuvable et l'archive ${S2A_FRONTEND_URL} "
                    "n'a pas pu être téléchargée.\n"
                    "Block editor: Node.js (pnpm) not found and the prebuilt archive could not be downloaded.\n"
                    "  - installe/install Node.js 20+ and pnpm (corepack enable)\n"
                    "  - ou/or -DS2A_FRONTEND_DIST=<folder with index.html>\n"
                    "  - ou/or -DS2A_BUILD_WEBVIEW=OFF")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${_s2a_archive}" DESTINATION "${_s2a_frontend_output}")
        endif()
        add_custom_target(s2a_frontend)
        message(STATUS "Frontend: prebuilt archive ${S2A_FRONTEND_URL}")
    endif()
endif()

function(s2a_bundle_frontend target)
    add_dependencies(${target} s2a_frontend)
    get_target_property(is_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND is_bundle)
        set(build_destination "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/frontend")
    else()
        set(build_destination "$<TARGET_FILE_DIR:${target}>/frontend")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${build_destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_s2a_frontend_output}" "${build_destination}"
        VERBATIM)

    if(APPLE AND is_bundle)
        install(DIRECTORY "${_s2a_frontend_output}/" DESTINATION "$<TARGET_FILE_NAME:${target}>.app/Contents/Resources/frontend")
    elseif(WIN32)
        install(DIRECTORY "${_s2a_frontend_output}/" DESTINATION "${CMAKE_INSTALL_BINDIR}/frontend")
    else()
        install(DIRECTORY "${_s2a_frontend_output}/" DESTINATION "${CMAKE_INSTALL_DATADIR}/scratch2arduino/frontend")
    endif()
endfunction()
