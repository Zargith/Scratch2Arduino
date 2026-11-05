# Scratch2Arduino - convenience wrapper around CMake (Linux and macOS).
#
#   make                         configure and build (Release)
#   make test                    run the unit tests
#   sudo make install            install the build (prefix chosen at `make` time)
#   sudo make uninstall          remove what `make install` installed
#   make debug                   Debug build in build/make-debug
#   make PREFIX=$HOME/.local WEBVIEW=OFF   options: PREFIX, WEBVIEW, TESTS, BUILD_TYPE
#
# For a one-command install with dependencies, use ./Scratch2Arduino_installer.sh instead.

CMAKE      ?= cmake
CTEST      ?= ctest
BUILD_TYPE ?= Release
BUILD_DIR  ?= build/make-$(shell echo $(BUILD_TYPE) | tr A-Z a-z)
# PREFIX (default /usr/local), WEBVIEW and TESTS (default ON) are only passed to CMake
# when given, so a later plain `make` or `make test` keeps the values chosen earlier.
PREFIX     ?=
WEBVIEW    ?=
TESTS      ?=
JOBS       ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
CMAKE_ARGS ?=

.PHONY: all configure build test install uninstall debug clean help

all: build

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" \
	    -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	    $(if $(PREFIX),-DCMAKE_INSTALL_PREFIX="$(PREFIX)") \
	    $(if $(WEBVIEW),-DS2A_BUILD_WEBVIEW="$(WEBVIEW)") \
	    $(if $(TESTS),-DS2A_BUILD_TESTS="$(TESTS)") \
	    $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --config $(BUILD_TYPE) --parallel $(JOBS)

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" -C $(BUILD_TYPE) --output-on-failure

# install/uninstall never rebuild, so `sudo make install` does not create root-owned build files.
install:
	@test -f "$(BUILD_DIR)/CMakeCache.txt" || { echo "Run 'make' first."; exit 1; }
	$(CMAKE) --install "$(BUILD_DIR)" --config $(BUILD_TYPE)

uninstall:
	@test -f "$(BUILD_DIR)/cmake_uninstall.cmake" || { echo "Nothing to uninstall: run 'make' and 'make install' first."; exit 1; }
	$(CMAKE) -P "$(BUILD_DIR)/cmake_uninstall.cmake"

debug:
	$(MAKE) build BUILD_TYPE=Debug

clean:
	$(CMAKE) -E rm -rf build/make-release build/make-debug

help:
	@sed -n '1,12p' Makefile
