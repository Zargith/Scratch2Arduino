#!/usr/bin/env bash
# Scratch2Arduino installer for Linux and macOS.
# Installs the build dependencies, builds Scratch2Arduino with CMake and installs it.
#
#   ./install.sh                    install system-wide (/usr/local on Linux; ~/Applications on macOS)
#   ./install.sh --user             install for the current user only (~/.local on Linux)
#   ./install.sh --uninstall        remove a previous installation
#   ./install.sh --help             all options
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OS="$(uname -s)"
PREFIX=""
INSTALL_DEPS=1
ASSUME_YES=0
UNINSTALL=0
BUILD_DIR="$ROOT_DIR/build/install-release"
CMAKE_MIN="3.25"

# --- Messages (French by default, English when the locale is English) ----------
case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in
    en*) LANG_UI=en ;;
    *) LANG_UI=fr ;;
esac
say() { if [ "$LANG_UI" = en ]; then printf '%s\n' "$2"; else printf '%s\n' "$1"; fi; }
step() { printf '\n\033[1;36m==> %s\033[0m\n' "$(say "$1" "$2")"; }
fail() { printf '\n\033[1;31m%s\033[0m\n' "$(say "$1" "$2")" >&2; exit 1; }

usage() {
    say "Utilisation : ./install.sh [options]" "Usage: ./install.sh [options]"
    cat <<'USAGE'
  --prefix DIR      installation prefix (macOS: folder receiving the .app bundle)
  --user            per-user install (Linux: ~/.local, macOS: ~/Applications)
  --no-deps         do not install system dependencies
  --build-dir DIR   build directory (default: build/install-release)
  -y, --yes         do not ask for confirmation when installing packages
  --uninstall       remove a previous installation
  -h, --help        show this help
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="${2:?--prefix needs a directory}"; shift ;;
        --prefix=*) PREFIX="${1#*=}" ;;
        --user) if [ "$OS" = Darwin ]; then PREFIX="$HOME/Applications"; else PREFIX="$HOME/.local"; fi ;;
        --no-deps) INSTALL_DEPS=0 ;;
        --build-dir) BUILD_DIR="${2:?--build-dir needs a directory}"; shift ;;
        -y|--yes) ASSUME_YES=1 ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage; fail "Option inconnue : $1" "Unknown option: $1" ;;
    esac
    shift
done

if [ -z "$PREFIX" ]; then
    if [ "$OS" = Darwin ]; then PREFIX="$HOME/Applications"; else PREFIX="/usr/local"; fi
fi

SUDO=""
if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1; then SUDO="sudo"; fi

# Runs a command as root only when the prefix is not writable by the current user.
as_prefix_owner() {
    local probe="$PREFIX"
    while [ ! -e "$probe" ]; do probe="$(dirname "$probe")"; done
    if [ -w "$probe" ]; then "$@"; else ${SUDO} "$@"; fi
}

# --- Uninstall -------------------------------------------------------------------
if [ "$UNINSTALL" -eq 1 ]; then
    step "Désinstallation de Scratch2Arduino" "Uninstalling Scratch2Arduino"
    [ -f "$BUILD_DIR/cmake_uninstall.cmake" ] ||
        fail "Aucune installation trouvée dans $BUILD_DIR." "No installation found in $BUILD_DIR."
    as_prefix_owner cmake -P "$BUILD_DIR/cmake_uninstall.cmake"
    say "Scratch2Arduino a été désinstallé." "Scratch2Arduino has been uninstalled."
    exit 0
fi

# --- Dependencies ----------------------------------------------------------------
install_linux_deps() {
    local yes_flag=() packages=()
    if command -v apt-get >/dev/null 2>&1; then
        if [ "$ASSUME_YES" -eq 1 ]; then yes_flag=(-y); fi
        packages=(build-essential cmake git pkg-config libwebkit2gtk-4.1-dev zenity)
        ${SUDO} apt-get update
        ${SUDO} apt-get install "${yes_flag[@]}" "${packages[@]}"
    elif command -v dnf >/dev/null 2>&1; then
        if [ "$ASSUME_YES" -eq 1 ]; then yes_flag=(-y); fi
        packages=(gcc-c++ make cmake git pkgconf-pkg-config webkit2gtk4.1-devel zenity)
        ${SUDO} dnf install "${yes_flag[@]}" "${packages[@]}"
    elif command -v pacman >/dev/null 2>&1; then
        if [ "$ASSUME_YES" -eq 1 ]; then yes_flag=(--noconfirm); fi
        packages=(base-devel cmake git pkgconf webkit2gtk-4.1 zenity)
        ${SUDO} pacman -S --needed "${yes_flag[@]}" "${packages[@]}"
    else
        fail "Gestionnaire de paquets non reconnu. Installe toi-même : un compilateur C++17, CMake >= $CMAKE_MIN, pkg-config et WebKitGTK 4.1, puis relance avec --no-deps." \
             "Unknown package manager. Install manually: a C++17 compiler, CMake >= $CMAKE_MIN, pkg-config and WebKitGTK 4.1, then run again with --no-deps."
    fi
}

install_macos_deps() {
    if ! xcode-select -p >/dev/null 2>&1; then
        xcode-select --install || true
        fail "Installe les outils de ligne de commande Xcode (une fenêtre s'est ouverte), puis relance ./install.sh." \
             "Install the Xcode command line tools (a window opened), then run ./install.sh again."
    fi
    command -v brew >/dev/null 2>&1 ||
        fail "Homebrew est requis : https://brew.sh" "Homebrew is required: https://brew.sh"
    brew install cmake
}

# True when everything needed to build is already installed, so updates skip the package manager.
deps_present() {
    command -v cmake >/dev/null 2>&1 || return 1
    command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || return 1
    if [ "$OS" = Linux ]; then
        command -v pkg-config >/dev/null 2>&1 || return 1
        pkg-config --exists webkit2gtk-4.1 || pkg-config --exists webkitgtk-6.0 || return 1
        command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1 || return 1
    fi
    return 0
}

if [ "$INSTALL_DEPS" -eq 1 ] && deps_present; then
    step "Dépendances déjà installées" "Dependencies already installed"
elif [ "$INSTALL_DEPS" -eq 1 ]; then
    step "Installation des dépendances" "Installing dependencies"
    case "$OS" in
        Linux) install_linux_deps ;;
        Darwin) install_macos_deps ;;
        *) fail "Système non pris en charge : $OS (sous Windows, utilise installer.exe)." \
                "Unsupported system: $OS (on Windows, use installer.exe)." ;;
    esac
fi

command -v cmake >/dev/null 2>&1 || fail "CMake est introuvable." "CMake not found."
cmake_version="$(cmake --version | head -n1 | awk '{print $3}')"
if [ "$(printf '%s\n%s\n' "$CMAKE_MIN" "$cmake_version" | sort -V | head -n1)" != "$CMAKE_MIN" ]; then
    fail "CMake $cmake_version est trop ancien (>= $CMAKE_MIN requis)." \
         "CMake $cmake_version is too old (>= $CMAKE_MIN required)."
fi

# --- Build -----------------------------------------------------------------------
cmake_args=(
    -S "$ROOT_DIR" -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_INSTALL_PREFIX=$PREFIX"
    -DS2A_BUILD_WEBVIEW=ON
    -DS2A_BUILD_TESTS=OFF
)

jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"

if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    say "Compilation précédente trouvée : seules les modifications seront recompilées." \
        "Previous build found: only the changes will be recompiled."
fi

step "Configuration (CMake)" "Configuring (CMake)"
cmake "${cmake_args[@]}"

step "Compilation" "Building"
cmake --build "$BUILD_DIR" --parallel "$jobs"

step "Installation dans $PREFIX" "Installing into $PREFIX"
as_prefix_owner cmake --install "$BUILD_DIR"
# Keep the build tree owned by the user even after a sudo install.
if [ -f "$BUILD_DIR/install_manifest.txt" ] && [ ! -O "$BUILD_DIR/install_manifest.txt" ]; then
    ${SUDO} chown "$(id -u):$(id -g)" "$BUILD_DIR/install_manifest.txt"
fi

# Versions before 0.4 installed two variants under other names: remove them.
for old in bin/scratch2arduino-webview bin/scratch2arduino-native \
           share/applications/scratch2arduino-webview.desktop share/applications/scratch2arduino-native.desktop \
           scratch2arduino-webview.app scratch2arduino-native.app; do
    if [ -e "$PREFIX/$old" ]; then as_prefix_owner rm -rf "${PREFIX:?}/$old"; fi
done

if [ "$OS" = Linux ] && command -v update-desktop-database >/dev/null 2>&1; then
    as_prefix_owner update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
fi

step "Terminé !" "Done!"
if [ "$OS" = Darwin ]; then
    say "L'application est dans $PREFIX." "The application is in $PREFIX."
else
    say "Lance : $PREFIX/bin/scratch2arduino" "Run: $PREFIX/bin/scratch2arduino"
    say "Elle apparaît aussi dans le menu des applications." "It also appears in the applications menu."
fi
say "Pour désinstaller : ./install.sh --uninstall" "To uninstall: ./install.sh --uninstall"
