#pragma once

#include "s2a/result.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace s2a::process {

/// An external program to run (arduino-cli, ...).
struct Command {
    std::filesystem::path program;
    std::vector<std::string> args;
    /// Added to (or overriding) the parent environment.
    std::map<std::string, std::string> env;
    /// Empty: the current directory.
    std::filesystem::path workingDirectory;
    /// 0: no limit. The program is stopped when the limit is reached.
    std::chrono::milliseconds timeout{0};
};

struct Output {
    int exitCode = -1;
    std::string out; ///< Everything written to stdout.
    std::string err; ///< Everything written to stderr.
    bool timedOut = false;

    [[nodiscard]] bool succeeded() const noexcept { return !timedOut && exitCode == 0; }
};

/// Runs a program and waits for it. Fails only when the program cannot be started; a program
/// that runs and fails is a successful Result with a non-zero exit code.
[[nodiscard]] Result<Output> run(const Command& command);

/// Something that runs commands. Production code uses run(); tests inject fakes.
using Runner = std::function<Result<Output>(const Command&)>;

/// The real runner (process::run).
[[nodiscard]] Runner systemRunner();

/// Looks for an executable in the PATH ("arduino-cli" finds "arduino-cli.exe" on Windows).
[[nodiscard]] std::filesystem::path findInPath(const std::string& name);

/// Directory containing the running executable (empty if unknown).
[[nodiscard]] std::filesystem::path executableDirectory();

} // namespace s2a::process
