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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return 2;
    }
    const std::string command = argv[1];
    if (command == "exit" && argc > 2) {
        return std::stoi(argv[2]);
    }
    if (command == "echo") {
        for (int i = 2; i < argc; ++i) {
            std::cout << argv[i] << '\n';
        }
        return 0;
    }
    if (command == "stderr" && argc > 2) {
        std::cerr << argv[2];
        return 0;
    }
    if (command == "env" && argc > 2) {
        const char* value = std::getenv(argv[2]); // NOLINT(concurrency-mt-unsafe)
        std::cout << (value != nullptr ? value : "<unset>");
        return 0;
    }
    if (command == "pwd") {
        std::cout << std::filesystem::current_path().u8string();
        return 0;
    }
    if (command == "sleep" && argc > 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[2])));
        return 0;
    }
    return 2;
}
