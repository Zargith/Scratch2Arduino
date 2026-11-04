#pragma once

#include "s2a/arduino_cli.hpp"
#include "s2a/i18n.hpp"
#include "s2a/project.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

/// Build and Upload buttons of the applications: block project -> sketch -> arduino-cli,
/// with every outcome explained in the user's language.
namespace s2a::toolchain {

enum class BuildStep {
    InstallingTools, ///< First build for a board family: downloading its core.
    Compiling,
    Uploading,
};

struct BuildOptions {
    std::filesystem::path workDirectory = userDataDirectory(); ///< Holds sketches/ and build/.
    std::string port;                                          ///< Upload to this port; empty = compile only.
    std::function<void(BuildStep)> progress;                   ///< Optional, called from the calling thread.
};

struct BuildResult {
    bool ok = false;
    std::string message;               ///< One friendly sentence for the status bar.
    std::string details;               ///< Technical output, for "show details" and bug reports.
    std::vector<std::string> blockIds; ///< Blocks the compiler errors point at, if any.
    std::filesystem::path sketch;      ///< Generated sketch folder.
};

[[nodiscard]] BuildResult buildProject(const ArduinoCli& cli, const project::Project& project,
                                       Language language, const BuildOptions& options = {});

} // namespace s2a::toolchain
