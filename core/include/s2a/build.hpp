#pragma once

#include "s2a/arduino_cli.hpp"
#include "s2a/i18n.hpp"
#include "s2a/project.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

/// Build and Upload buttons of the applications: block project -> sketch -> arduino-cli,
/// with every outcome explained in the user's language. A C++ project is compiled as typed; a
/// Python or JavaScript project is only checked (it runs on the computer, in Live mode).
namespace s2a::toolchain {

enum class BuildStep {
    InstallingTools,     ///< First build for a board family: downloading its core.
    InstallingLibraries, ///< First build using a component library (Servo...): downloading it.
    Compiling,
    Uploading,
};

struct BuildOptions {
    std::filesystem::path workDirectory = userDataDirectory(); ///< Holds sketches/ and build/.
    std::string port;                                          ///< Upload to this port; empty = compile only.
    std::function<void(BuildStep)> progress;                   ///< Optional, called from the calling thread.
};

/// A mistake at a line of a program typed as text (C++, Python, JavaScript).
struct CodeProblem {
    int line = 0;   ///< 1-based; 0 when the place is unknown.
    int column = 0; ///< 1-based; 0 when unknown.
    std::string message; ///< In the user's language when it could be explained.
    std::string file;    ///< "" for the program itself, "user" for the "My code" file.
};

struct BuildResult {
    bool ok = false;
    std::string message;               ///< One friendly sentence for the status bar.
    std::string details;               ///< Technical output, for "show details" and bug reports.
    std::vector<std::string> blockIds; ///< Blocks to show: validation errors, or where the compiler errors point.
    std::vector<std::string> warnings; ///< Validation warnings (the program was built anyway).
    std::vector<std::string> warningBlockIds;
    std::filesystem::path sketch;      ///< Generated sketch folder.
    std::vector<CodeProblem> problems; ///< Text programs: the faulty lines, shown in the code editor.
};

[[nodiscard]] BuildResult buildProject(const ArduinoCli& cli, const project::Project& project,
                                       Language language, const BuildOptions& options = {});

} // namespace s2a::toolchain
