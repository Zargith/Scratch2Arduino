#pragma once

#include "s2a/arduino_cli.hpp"
#include "s2a/i18n.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace s2a::toolchain {

/// What went wrong while installing, compiling or uploading, in terms a beginner can act on.
enum class Problem {
    None,
    PlatformMissing,      ///< The board's core is not installed.
    LibraryMissing,       ///< #include of a library that is not installed.
    CompilerError,        ///< The generated code does not compile.
    SketchTooBig,         ///< Program storage is full.
    NotEnoughMemory,      ///< RAM is full.
    PortNotFound,         ///< The board is unplugged or the port changed.
    PortBusy,             ///< Another program (serial monitor...) uses the port.
    PortPermissionDenied, ///< Linux: the user is not in the dialout group.
    BoardNotResponding,   ///< Wrong board selected, bad cable, or the board is stuck.
    NetworkUnavailable,   ///< Downloading a core or library failed.
    Timeout,              ///< The tool took too long and was stopped.
    Unknown,
};

/// One message of the compiler ("file:line:column: error: text").
struct CompilerMessage {
    std::string file;
    int line = 0;
    int column = 0;
    std::string severity; ///< "error", "fatal error", "warning" or "note".
    std::string text;
};

/// Parses GCC diagnostics; other lines (context, carets) are ignored.
[[nodiscard]] std::vector<CompilerMessage> parseCompilerMessages(std::string_view output);

struct Diagnosis {
    Problem problem = Problem::None;
    TranslationParams params;              ///< Values for the message placeholders.
    std::vector<CompilerMessage> messages; ///< Compiler errors, for CompilerError.
    std::string technical;                 ///< Raw tool output, for "show details" and bug reports.
};

[[nodiscard]] Diagnosis diagnoseInstall(const CommandOutcome& outcome);
[[nodiscard]] Diagnosis diagnoseCompile(const CompileOutcome& outcome);
[[nodiscard]] Diagnosis diagnoseUpload(const CommandOutcome& outcome, std::string_view port);

/// One compiler error of a program typed as text, in words a beginner can act on: common
/// mistakes ("'x' was not declared", a missing ';' or '}') are explained in `language`, other
/// messages are given as the compiler wrote them.
[[nodiscard]] std::string explainCompilerMessage(const CompilerMessage& message, Language language);

/// Translation key of the message for a problem ("toolchain.port_busy").
[[nodiscard]] std::string_view problemMessageKey(Problem problem) noexcept;

/// The friendly message, with its hint, in `language`.
[[nodiscard]] std::string describe(const Diagnosis& diagnosis, Language language);

} // namespace s2a::toolchain
