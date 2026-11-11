# Third-party libraries of s2a_core, fetched at configure time (pinned versions and hashes).
include(FetchContent)

# nlohmann/json - JSON parsing and serialization (MIT).
find_package(nlohmann_json 3.11 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# miniz - zip archives for .s2a project files (MIT). The release ships amalgamated sources.
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.1.0/miniz-3.1.0.zip
    URL_HASH SHA256=f9d3e769ec5fd5583955a3c22eea457fe806be0d507805966d1a5b0410c77dda
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(miniz)
if(NOT TARGET s2a_miniz)
    add_library(s2a_miniz STATIC "${miniz_SOURCE_DIR}/miniz.c")
    target_include_directories(s2a_miniz SYSTEM PUBLIC "${miniz_SOURCE_DIR}")
    set_target_properties(s2a_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
    # Only the in-memory zip APIs are used; files are read and written with std::filesystem
    # (Unicode paths on Windows).
    # MINIZ_NO_TIME: entries get a zero timestamp, so saving the same project gives the same bytes.
    target_compile_definitions(s2a_miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
    if(MSVC)
        target_compile_options(s2a_miniz PRIVATE /W0)
    else()
        target_compile_options(s2a_miniz PRIVATE -w)
    endif()
endif()

# reproc - run external programs (arduino-cli) portably, with captured output and timeouts (MIT).
set(REPROC++ ON CACHE BOOL "" FORCE)
set(REPROC_TEST OFF CACHE BOOL "" FORCE)
set(REPROC_EXAMPLES OFF CACHE BOOL "" FORCE)
set(REPROC_INSTALL OFF CACHE BOOL "" FORCE)
set(REPROC_MULTITHREADED ON CACHE BOOL "" FORCE)
FetchContent_Declare(reproc
    URL https://github.com/DaanDeMeyer/reproc/archive/refs/tags/v14.2.7.tar.gz
    URL_HASH SHA256=77914ba566356d86986db240ecdb1762dfdd55bb1e8e127d016d9c5c4c9300e8
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(reproc)

# pocketpy - Python interpreter of the Python mode (MIT). The release ships one C file and its
# header; they are compiled through core/third_party/s2a_pocketpy.c (see there why).
FetchContent_Declare(pocketpy_source
    URL https://github.com/pocketpy/pocketpy/releases/download/v2.2.0/pocketpy.c
    URL_HASH SHA256=0f0c19071b4fd0b37cb292e15bea49ca163f151af901b00893d3a2005bb056e6
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_Declare(pocketpy_header
    URL https://github.com/pocketpy/pocketpy/releases/download/v2.2.0/pocketpy.h
    URL_HASH SHA256=43864cfa090d65b80915467900430dfae52c769f13d6edb137d0227d1fbe03ad
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(pocketpy_source pocketpy_header)
if(NOT TARGET s2a_pocketpy)
    add_library(s2a_pocketpy STATIC "${PROJECT_SOURCE_DIR}/core/third_party/s2a_pocketpy.c")
    target_include_directories(s2a_pocketpy PRIVATE "${pocketpy_source_SOURCE_DIR}")
    target_include_directories(s2a_pocketpy SYSTEM PUBLIC "${pocketpy_header_SOURCE_DIR}")
    set_target_properties(s2a_pocketpy PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11 C_STANDARD_REQUIRED ON)
    # No os/io modules: a script drives the board, not the computer's files. The watchdog stops
    # a running script (the Stop button, an endless loop). NDEBUG: pocketpy's own debug checks
    # slow every step down, even in Debug builds of Scratch2Arduino.
    target_compile_definitions(s2a_pocketpy PUBLIC PK_ENABLE_OS=0 PK_ENABLE_WATCHDOG=1 PRIVATE NDEBUG)
    if(MSVC)
        target_compile_options(s2a_pocketpy PRIVATE /W0 /utf-8)
    else()
        target_compile_options(s2a_pocketpy PRIVATE -w)
        target_link_libraries(s2a_pocketpy PRIVATE m)
    endif()
endif()

# QuickJS-ng - JavaScript interpreter of the JavaScript mode (MIT).
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(quickjs
    URL https://github.com/quickjs-ng/quickjs/archive/refs/tags/v0.17.0.tar.gz
    URL_HASH SHA256=559bc4c420475e55c7ab4510adbc562f55d7524d75e8e89d79ce4bb02f5687d9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(quickjs)
if(MSVC)
    target_compile_options(qjs PRIVATE /W0)
else()
    target_compile_options(qjs PRIVATE -w)
endif()
