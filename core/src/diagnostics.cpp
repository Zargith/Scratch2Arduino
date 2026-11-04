#include "s2a/diagnostics.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace s2a::toolchain {

namespace {

bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

bool containsAny(std::string_view text, std::initializer_list<std::string_view> needles) {
    return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) { return contains(text, needle); });
}

/// True when one line of `text` contains one of `needles` and one of `context`.
bool lineContains(std::string_view text, std::initializer_list<std::string_view> needles,
                  std::initializer_list<std::string_view> context) {
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = text.substr(start, end - start);
        if (containsAny(line, needles) && containsAny(line, context)) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

std::string allText(const CommandOutcome& outcome) {
    return outcome.error + "\n" + outcome.err + "\n" + outcome.out;
}

/// Problems shared by every command (downloads, timeouts, missing core).
Problem commonProblem(const CommandOutcome& outcome, std::string_view text) {
    if (outcome.timedOut) {
        return Problem::Timeout;
    }
    if (containsAny(text, {"platform not installed", "Platform '", "platform not found"}) &&
        containsAny(text, {"not installed", "not found"})) {
        return Problem::PlatformMissing;
    }
    // "Error downloading" / "Error updating index" name the operation, not the cause (a full
    // disk or a permission problem gives them too): only network causes count.
    if (containsAny(text, {"dial tcp", "no such host", "connection refused", "connection reset", "i/o timeout",
                           "network is unreachable", "TLS handshake timeout", "Temporary failure in name resolution",
                           "server misbehaving", "context deadline exceeded"})) {
        return Problem::NetworkUnavailable;
    }
    return Problem::None;
}

Diagnosis make(Problem problem, const CommandOutcome& outcome) {
    Diagnosis diagnosis;
    diagnosis.problem = problem;
    diagnosis.technical = allText(outcome);
    return diagnosis;
}

} // namespace

std::vector<CompilerMessage> parseCompilerMessages(std::string_view output) {
    // /path/sketch.ino:12:5: error: 'pinMod' was not declared in this scope
    // C:\path\sketch.ino:12:5: fatal error: Servo.h: No such file or directory
    static const std::regex pattern(R"(^(.+?):(\d+):(\d+): (fatal error|error|warning|note): (.*)$)");
    std::vector<CompilerMessage> messages;
    std::size_t start = 0;
    while (start < output.size()) {
        std::size_t end = output.find('\n', start);
        if (end == std::string_view::npos) {
            end = output.size();
        }
        std::string line(output.substr(start, end - start));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::smatch match;
        if (std::regex_match(line, match, pattern)) {
            messages.push_back({match[1].str(), std::stoi(match[2].str()), std::stoi(match[3].str()), match[4].str(),
                                match[5].str()});
        }
        start = end + 1;
    }
    return messages;
}

Diagnosis diagnoseInstall(const CommandOutcome& outcome) {
    if (outcome.success) {
        return {};
    }
    const std::string text = allText(outcome);
    const Problem common = commonProblem(outcome, text);
    return make(common != Problem::None ? common : Problem::Unknown, outcome);
}

Diagnosis diagnoseCompile(const CompileOutcome& outcome) {
    if (outcome.success) {
        return {};
    }
    const std::string text = allText(outcome);
    if (const Problem common = commonProblem(outcome, text); common != Problem::None) {
        return make(common, outcome);
    }
    // Memory problems are reported by arduino-cli after a successful compilation.
    // ... or by the AVR linker when it overflows: "region `text' overflowed by 4096 bytes",
    // "section `.bss' is not within region `data'".
    if (containsAny(text, {"Sketch too big", "text section exceeds available space", "region `text' overflowed",
                           "will not fit in region `text'"})) {
        return make(Problem::SketchTooBig, outcome);
    }
    if (containsAny(text, {"Not enough memory", "data section exceeds available space", "region `data' overflowed",
                           "is not within region `data'", "will not fit in region `data'"})) {
        return make(Problem::NotEnoughMemory, outcome);
    }

    Diagnosis diagnosis = make(Problem::Unknown, outcome);
    auto messages = parseCompilerMessages(outcome.err);
    // fatal error: Servo.h: No such file or directory -> library "Servo" is missing.
    static const std::regex missingHeader(R"(^([\w./+-]+)\.h(?:pp)?: No such file or directory$)");
    for (const auto& message : messages) {
        std::smatch match;
        if (message.severity == "fatal error" && std::regex_match(message.text, match, missingHeader)) {
            diagnosis.problem = Problem::LibraryMissing;
            diagnosis.params = {{"library", match[1].str()}};
            diagnosis.messages = {message};
            return diagnosis;
        }
    }
    messages.erase(std::remove_if(messages.begin(), messages.end(),
                                  [](const CompilerMessage& message) {
                                      return message.severity != "error" && message.severity != "fatal error";
                                  }),
                   messages.end());
    if (!messages.empty()) {
        diagnosis.problem = Problem::CompilerError;
        diagnosis.params = {{"count", std::to_string(messages.size())}};
        diagnosis.messages = std::move(messages);
    }
    return diagnosis;
}

Diagnosis diagnoseUpload(const CommandOutcome& outcome, std::string_view port) {
    if (outcome.success) {
        return {};
    }
    const std::string text = allText(outcome);
    Problem problem = commonProblem(outcome, text);
    if (problem == Problem::None) {
        if (containsAny(text, {"Permission denied", "permission denied"})) {
            problem = Problem::PortPermissionDenied;
        } else if (containsAny(text, {"Device or resource busy", "Resource busy", "Access is denied",
                                      "Accès refusé", "port is busy"})) {
            problem = Problem::PortBusy;
        } else if (containsAny(text, {"could not open port", "cannot open port", "unable to open port",
                                      "port not found", "Port not found", "no such port"}) ||
                   // A missing file is a missing port only when the line is about the port, not
                   // e.g. "avrdude: can't open input file sketch.ino.hex: No such file or directory".
                   lineContains(text, {"No such file or directory", "cannot find the file specified"},
                                {port.empty() ? std::string_view("\x01") : port, "ser_open", "serial port"})) {
            problem = Problem::PortNotFound;
        } else if (containsAny(text, {"not responding", "not in sync", "stk500_recv", "stk500v2_ReceiveMessage",
                                      "stk500v2_getsync", "attempt 10 of 10", "programmer is not responding"})) {
            problem = Problem::BoardNotResponding;
        } else {
            problem = Problem::Unknown;
        }
    }
    Diagnosis diagnosis = make(problem, outcome);
    diagnosis.params = {{"port", std::string(port)}};
    return diagnosis;
}

std::string_view problemMessageKey(Problem problem) noexcept {
    switch (problem) {
    case Problem::None:
        return "toolchain.none";
    case Problem::PlatformMissing:
        return "toolchain.platform_missing";
    case Problem::LibraryMissing:
        return "toolchain.library_missing";
    case Problem::CompilerError:
        return "toolchain.compiler_error";
    case Problem::SketchTooBig:
        return "toolchain.sketch_too_big";
    case Problem::NotEnoughMemory:
        return "toolchain.not_enough_memory";
    case Problem::PortNotFound:
        return "toolchain.port_not_found";
    case Problem::PortBusy:
        return "toolchain.port_busy";
    case Problem::PortPermissionDenied:
        return "toolchain.port_permission_denied";
    case Problem::BoardNotResponding:
        return "toolchain.board_not_responding";
    case Problem::NetworkUnavailable:
        return "toolchain.network_unavailable";
    case Problem::Timeout:
        return "toolchain.timeout";
    case Problem::Unknown:
        return "toolchain.unknown";
    }
    return "toolchain.unknown";
}

std::string describe(const Diagnosis& diagnosis, Language language) {
    return tr(problemMessageKey(diagnosis.problem), language, diagnosis.params);
}

} // namespace s2a::toolchain
