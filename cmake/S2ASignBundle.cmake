# Run by CPack before the macOS .dmg is made (CPACK_PRE_BUILD_SCRIPTS): signs Scratch2Arduino.app ad
# hoc ("-": no certificate). The linker signs the executable alone; arduino-cli, s2a-cli and the block
# editor are added to the bundle afterwards, and an unsigned bundle downloaded from the Internet is
# reported as "damaged". Signed ad hoc, it is only from an "unidentified developer" (Open Anyway).
file(GLOB_RECURSE _s2a_plists LIST_DIRECTORIES false "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*Scratch2Arduino.app/Contents/Info.plist")
if(NOT _s2a_plists)
    message(FATAL_ERROR "Scratch2Arduino.app not found in ${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
endif()
foreach(_s2a_plist IN LISTS _s2a_plists)
    get_filename_component(_s2a_contents "${_s2a_plist}" DIRECTORY)
    get_filename_component(_s2a_app "${_s2a_contents}" DIRECTORY)
    message(STATUS "Signing ${_s2a_app} (ad hoc)")
    execute_process(COMMAND codesign --force --deep --sign - "${_s2a_app}" RESULT_VARIABLE _s2a_result)
    if(NOT _s2a_result EQUAL 0)
        message(FATAL_ERROR "codesign failed on ${_s2a_app}")
    endif()
    execute_process(COMMAND codesign --verify --deep --strict "${_s2a_app}" RESULT_VARIABLE _s2a_result)
    if(NOT _s2a_result EQUAL 0)
        message(FATAL_ERROR "the signature of ${_s2a_app} does not verify")
    endif()
endforeach()
