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
