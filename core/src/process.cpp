#include "s2a/process.hpp"

#include <cstdlib>
#include <reproc++/run.hpp>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#else
#include <unistd.h>
#endif

namespace s2a::process {

Result<Output> run(const Command& command) {
    std::vector<std::string> arguments;
    arguments.reserve(command.args.size() + 1);
    arguments.push_back(command.program.u8string());
    arguments.insert(arguments.end(), command.args.begin(), command.args.end());

    reproc::options options;
    options.env.behavior = reproc::env::extend;
    options.env.extra = reproc::env(command.env);
    const std::string workingDirectory = command.workingDirectory.u8string();
    if (!workingDirectory.empty()) {
        options.working_directory = workingDirectory.c_str();
    }
    if (command.timeout.count() > 0) {
        options.deadline = reproc::milliseconds(command.timeout.count());
    }
    // Capture both output streams; no input, so a tool can never block waiting for the keyboard.
    options.redirect.in.type = reproc::redirect::discard;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;
    // When the deadline is reached (or on early return): ask politely, then force.
    options.stop = {{reproc::stop::wait, reproc::milliseconds(0)},
                    {reproc::stop::terminate, reproc::milliseconds(2000)},
                    {reproc::stop::kill, reproc::milliseconds(2000)}};

    Output output;
    reproc::process process;
    std::error_code error = process.start(arguments, options);
    if (error) {
        return Error{ErrorCode::ProcessFailed,
                     "cannot start " + command.program.u8string() + ": " + error.message()};
    }
    reproc::sink::string outSink(output.out);
    reproc::sink::string errSink(output.err);
    error = reproc::drain(process, outSink, errSink);
    if (error == std::errc::timed_out) {
        output.timedOut = true;
    } else if (error) {
        return Error{ErrorCode::ProcessFailed,
                     "error while running " + command.program.u8string() + ": " + error.message()};
    }
    if (output.timedOut) {
        (void)process.stop(options.stop);
        return output;
    }
    const auto [status, stopError] = process.wait(reproc::infinite);
    if (stopError) {
        return Error{ErrorCode::ProcessFailed,
                     "cannot wait for " + command.program.u8string() + ": " + stopError.message()};
    }
    output.exitCode = status;
    return output;
}

Runner systemRunner() {
    return [](const Command& command) { return run(command); };
}

std::filesystem::path findInPath(const std::string& name) {
    const char* path = std::getenv("PATH"); // NOLINT(concurrency-mt-unsafe)
    if (path == nullptr) {
        return {};
    }
#ifdef _WIN32
    constexpr char kSeparator = ';';
    const std::vector<std::string> suffixes{".exe", ".cmd", ".bat", ""};
#else
    constexpr char kSeparator = ':';
    const std::vector<std::string> suffixes{""};
#endif
    std::stringstream entries(path);
    std::string directory;
    while (std::getline(entries, directory, kSeparator)) {
        if (directory.empty()) {
            continue;
        }
        for (const auto& suffix : suffixes) {
            const std::filesystem::path candidate = std::filesystem::u8path(directory) / (name + suffix);
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) {
                return candidate;
            }
        }
    }
    return {};
}

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    std::error_code error;
    const auto resolved = std::filesystem::canonical(buffer.c_str(), error);
    return error ? std::filesystem::path{} : resolved.parent_path();
#else
    std::error_code error;
    const auto resolved = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : resolved.parent_path();
#endif
}

} // namespace s2a::process
