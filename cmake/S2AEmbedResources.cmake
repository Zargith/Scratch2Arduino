# s2a_embed_resources(<target> BASE_DIR <dir> FILES <file>...)
#
# Compiles data files (JSON catalogs, board descriptions, translations) into <target>, so the
# applications never depend on files found at runtime. Each file is reachable through
# s2a::resources::find("<path relative to BASE_DIR>") - see core/include/s2a/resources.hpp.
# Generated at configure time; editing an embedded file re-runs CMake automatically.
function(s2a_embed_resources target)
    cmake_parse_arguments(ARG "" "BASE_DIR" "FILES" ${ARGN})
    if(NOT ARG_BASE_DIR)
        message(FATAL_ERROR "s2a_embed_resources: BASE_DIR is required")
    endif()

    list(SORT ARG_FILES)
    set(arrays "")
    set(entries "")
    set(index 0)
    foreach(file IN LISTS ARG_FILES)
        get_filename_component(absolute "${file}" ABSOLUTE BASE_DIR "${ARG_BASE_DIR}")
        file(RELATIVE_PATH relative "${ARG_BASE_DIR}" "${absolute}")
        file(READ "${absolute}" hex HEX)
        string(LENGTH "${hex}" hex_length)
        math(EXPR size "${hex_length} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
        # A trailing 0 keeps empty files valid C++ (zero-length arrays are not).
        string(APPEND arrays "const unsigned char kData${index}[] = {${bytes}0x00};\n")
        string(APPEND entries "    {\"${relative}\", kData${index}, ${size}},\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${absolute}")
        math(EXPR index "${index} + 1")
    endforeach()

    set(S2A_RESOURCE_ARRAYS "${arrays}")
    set(S2A_RESOURCE_ENTRIES "${entries}")
    set(S2A_RESOURCE_COUNT ${index})
    set(output "${CMAKE_CURRENT_BINARY_DIR}/generated/s2a_embedded_resources.cpp")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/S2AEmbeddedResources.cpp.in" "${output}" @ONLY)
    target_sources(${target} PRIVATE "${output}")
endfunction()
