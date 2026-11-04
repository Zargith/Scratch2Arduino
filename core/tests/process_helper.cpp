// Small program driven by test_process.cpp:
//   process_helper exit <code>          exits with <code>
//   process_helper echo <args...>       prints its arguments on stdout, one per line
//   process_helper stderr <text>        prints <text> on stderr
//   process_helper env <name>           prints the value of an environment variable
//   process_helper pwd                  prints the working directory
//   process_helper sleep <ms>           sleeps
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

// On Windows, argv is encoded in the ANSI code page: read the UTF-16 command
// line instead and convert it to UTF-8, like real tools (arduino-cli) do.
std::vector<std::string> utf8Arguments(int argc, char* argv[]) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    std::vector<std::string> result;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide == nullptr) {
        return result;
    }
    for (int i = 0; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(size > 0 ? static_cast<std::size_t>(size - 1) : 0U, '\0');
        if (size > 1) {
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), size, nullptr, nullptr);
        }
        result.push_back(std::move(arg));
    }
    LocalFree(static_cast<HLOCAL>(wide));
    return result;
#else
    return {argv, argv + argc};
#endif
}

} // namespace

int main(int argc, char* argv[]) {
    const std::vector<std::string> args = utf8Arguments(argc, argv);
    argc = static_cast<int>(args.size());
    if (argc < 2) {
        return 2;
    }
    const std::string command = args[1];
    if (command == "exit" && argc > 2) {
        return std::stoi(args[2]);
    }
    if (command == "echo") {
        for (int i = 2; i < argc; ++i) {
            std::cout << args[i] << '\n';
        }
        return 0;
    }
    if (command == "stderr" && argc > 2) {
        std::cerr << args[2];
        return 0;
    }
    if (command == "env" && argc > 2) {
        const char* value = std::getenv(args[2].c_str()); // NOLINT(concurrency-mt-unsafe)
        std::cout << (value != nullptr ? value : "<unset>");
        return 0;
    }
    if (command == "pwd") {
        std::cout << std::filesystem::current_path().u8string();
        return 0;
    }
    if (command == "sleep" && argc > 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(args[2])));
        return 0;
    }
    return 2;
}
