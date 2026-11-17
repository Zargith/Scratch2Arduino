# Makes the board core seed shipped with Scratch2Arduino (see S2A_BUNDLE_ARDUINO_CORES in
# S2AArduinoCli.cmake). Run at build time:
#   cmake -DCLI=<arduino-cli> -DPLATFORMS=arduino:avr[,...] -DWORK=<scratch dir> -DSEED=<output dir> -P S2AArduinoSeed.cmake
# Output: <SEED>/downloads/** (archives), <SEED>/data/package_*index.json (board indexes) and
# <SEED>/platforms.txt (one "vendor:arch@version" per line, read by ArduinoCli::seededVersion()).
foreach(variable CLI PLATFORMS WORK SEED)
    if(NOT ${variable})
        message(FATAL_ERROR "S2AArduinoSeed.cmake: ${variable} is not set")
    endif()
endforeach()
string(REPLACE "," ";" PLATFORMS "${PLATFORMS}")

# Same additional board indexes as ArduinoCli::run() (core/src/arduino_cli.cpp).
set(ENV{ARDUINO_DIRECTORIES_DATA} "${WORK}/data")
set(ENV{ARDUINO_DIRECTORIES_DOWNLOADS} "${WORK}/downloads")
set(ENV{ARDUINO_DIRECTORIES_USER} "${WORK}/user")
set(ENV{ARDUINO_UPDATER_ENABLE_NOTIFICATION} "false")
set(ENV{ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS}
    "https://espressif.github.io/arduino-esp32/package_esp32_index.json https://arduino.esp8266.com/stable/package_esp8266com_index.json")

function(run_cli)
    execute_process(COMMAND "${CLI}" ${ARGN} --no-color
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "arduino-cli ${ARGN} failed (${result}):\n${output}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK}" "${SEED}")
run_cli(core update-index)
foreach(platform IN LISTS PLATFORMS)
    message(STATUS "Seed: installing ${platform}")
    run_cli(core install "${platform}")
endforeach()

execute_process(COMMAND "${CLI}" core list --json --no-color
                RESULT_VARIABLE result OUTPUT_VARIABLE list)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "arduino-cli core list failed (${result})")
endif()
set(lines "")
foreach(platform IN LISTS PLATFORMS)
    set(version "")
    string(JSON count ERROR_VARIABLE error LENGTH "${list}" platforms)
    if(NOT error AND count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE ${last})
            string(JSON id GET "${list}" platforms ${index} id)
            if(id STREQUAL platform)
                string(JSON version GET "${list}" platforms ${index} installed_version)
            endif()
        endforeach()
    endif()
    if(NOT version)
        message(FATAL_ERROR "Seed: ${platform} is not installed after 'arduino-cli core install'")
    endif()
    string(APPEND lines "${platform}@${version}\n")
endforeach()

file(COPY "${WORK}/downloads/" DESTINATION "${SEED}/downloads")
file(GLOB indexes "${WORK}/data/package_*index.json" "${WORK}/data/package_*index.json.sig")
file(COPY ${indexes} DESTINATION "${SEED}/data")
# Written last: it is the output the build checks, so an interrupted run starts again.
file(WRITE "${SEED}/platforms.txt" "${lines}")
file(REMOVE_RECURSE "${WORK}")
