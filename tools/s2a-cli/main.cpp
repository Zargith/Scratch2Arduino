// s2a-cli - Scratch2Arduino toolchain from the command line.
//
//   s2a-cli boards                                 supported boards
//   s2a-cli ports                                  plugged-in boards
//   s2a-cli setup [--board <id>]                   install the tools for a board (all by default)
//   s2a-cli compile <sketch> [--board <id>]        compile an Arduino sketch folder
//   s2a-cli upload <sketch> --port <port> [--board <id>]
//   options: --lang fr|en, --verbose
#include "s2a/arduino_cli.hpp"
#include "s2a/boards.hpp"
#include "s2a/diagnostics.hpp"
#include "s2a/i18n.hpp"
#include "s2a/version.hpp"

#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

namespace fs = std::filesystem;
using s2a::Language;
using s2a::toolchain::ArduinoCli;

constexpr int kSuccess = 0;
constexpr int kFailure = 1;
constexpr int kUsage = 2;

struct Options {
    std::string command;
    std::vector<std::string> positional;
    std::string boardId{s2a::BoardDatabase::kDefaultBoardId};
    std::string port;
    Language language = s2a::detectSystemLanguage();
    bool verbose = false;
    bool boardGiven = false;
};

/// Absolute sketch folder: "." and "blink/" become the folder itself.
fs::path sketchPath(const std::string& argument) {
    fs::path path = fs::absolute(fs::u8path(argument)).lexically_normal();
    if (!path.has_filename() && path.has_parent_path()) {
        path = path.parent_path();
    }
    return path;
}

/// Build folder for one sketch and board. Two sketches with the same name in different
/// folders must not share it (one compilation would upload the other's binary).
fs::path buildDirectory(const fs::path& sketch, const s2a::Board& board) {
    const std::size_t hash = std::hash<std::string>{}(sketch.generic_u8string());
    return s2a::toolchain::userDataDirectory() / "build" /
           (sketch.filename().u8string() + "-" + board.id + "-" + std::to_string(hash % 1000000007U));
}

class Cli {
public:
    explicit Cli(Options options) : options_(std::move(options)) {}

    int run();

private:
    [[nodiscard]] std::string tr(std::string_view key, const s2a::TranslationParams& params = {}) const {
        return s2a::tr(key, options_.language, params);
    }
    void print(std::string_view key, const s2a::TranslationParams& params = {}) const {
        std::cout << tr(key, params) << '\n';
    }
    void fail(std::string_view key, const s2a::TranslationParams& params = {}) const {
        std::cerr << tr(key, params) << '\n';
    }
    int report(const s2a::toolchain::Diagnosis& diagnosis) const {
        std::cerr << s2a::toolchain::describe(diagnosis, options_.language) << '\n';
        for (const auto& message : diagnosis.messages) {
            std::cerr << "  " << fs::u8path(message.file).filename().u8string() << ':' << message.line << ": "
                      << message.text << '\n';
        }
        if (options_.verbose) {
            std::cerr << "\n" << diagnosis.technical << '\n';
        }
        return kFailure;
    }

    [[nodiscard]] const s2a::Board* board() const;
    [[nodiscard]] std::optional<ArduinoCli> arduinoCli() const;
    int ensurePlatform(const ArduinoCli& cli, const s2a::Board& board) const;
    int compile(const ArduinoCli& cli, const s2a::Board& board, const fs::path& sketch, const fs::path& build) const;

    int boards() const;
    int ports() const;
    int setup() const;
    int compileCommand() const;
    int uploadCommand() const;
    int version() const;

    Options options_;
};

const s2a::Board* Cli::board() const {
    const auto& database = s2a::BoardDatabase::builtin();
    if (const auto* found = database.find(options_.boardId)) {
        return found;
    }
    std::string ids;
    for (const auto& known : database.boards()) {
        ids += (ids.empty() ? "" : ", ") + known.id;
    }
    fail("cli.unknown_board", {{"board", options_.boardId}, {"boards", ids}});
    return nullptr;
}

std::optional<ArduinoCli> Cli::arduinoCli() const {
    const fs::path executable = s2a::toolchain::locateArduinoCli();
    if (executable.empty()) {
        fail("cli.arduino_cli_missing");
        return std::nullopt;
    }
    return ArduinoCli(executable, s2a::toolchain::Directories::defaults());
}

int Cli::ensurePlatform(const ArduinoCli& cli, const s2a::Board& board) const {
    const auto installed = cli.isPlatformInstalled(board.platform);
    if (!installed.ok()) {
        std::cerr << tr(s2a::errorMessageKey(installed.error().code)) << '\n';
        if (options_.verbose) {
            std::cerr << installed.error().detail << '\n';
        }
        return kFailure;
    }
    if (installed.value()) {
        return kSuccess;
    }
    print("cli.installing_platform", {{"board", board.name}});
    const auto outcome = cli.installPlatform(board.platform);
    if (!outcome.ok()) {
        std::cerr << tr(s2a::errorMessageKey(outcome.error().code)) << '\n';
        return kFailure;
    }
    if (const auto diagnosis = s2a::toolchain::diagnoseInstall(outcome.value());
        diagnosis.problem != s2a::toolchain::Problem::None) {
        return report(diagnosis);
    }
    return kSuccess;
}

int Cli::compile(const ArduinoCli& cli, const s2a::Board& board, const fs::path& sketch, const fs::path& build) const {
    std::error_code error;
    if (!fs::is_directory(sketch, error) || !fs::exists(sketch / (sketch.filename().u8string() + ".ino"), error)) {
        fail("cli.sketch_missing", {{"sketch", sketch.u8string()}});
        return kFailure;
    }
    if (const int status = ensurePlatform(cli, board); status != kSuccess) {
        return status;
    }
    print("cli.compiling", {{"sketch", sketch.filename().u8string()}, {"board", board.name}});
    const auto outcome = cli.compile(sketch, board, build);
    if (!outcome.ok()) {
        std::cerr << tr(s2a::errorMessageKey(outcome.error().code)) << '\n';
        return kFailure;
    }
    if (const auto diagnosis = s2a::toolchain::diagnoseCompile(outcome.value());
        diagnosis.problem != s2a::toolchain::Problem::None) {
        return report(diagnosis);
    }
    s2a::TranslationParams params;
    for (const auto& section : outcome.value().memory) {
        const std::string percent = section.max > 0 ? std::to_string(section.size * 100 / section.max) : "?";
        params.emplace_back(section.section, std::to_string(section.size));
        params.emplace_back(section.section + "_max", std::to_string(section.max));
        params.emplace_back(section.section + "_percent", percent);
    }
    print("cli.compiled", params);
    return kSuccess;
}

int Cli::boards() const {
    for (const auto& known : s2a::BoardDatabase::builtin().boards()) {
        std::cout << known.id << "\t" << known.name << "\t" << known.fqbn << '\n';
    }
    return kSuccess;
}

int Cli::ports() const {
    const auto cli = arduinoCli();
    if (!cli) {
        return kFailure;
    }
    const auto ports = cli->detectPorts();
    if (!ports.ok()) {
        std::cerr << tr(s2a::errorMessageKey(ports.error().code)) << '\n';
        return kFailure;
    }
    bool any = false;
    for (const auto& port : ports.value()) {
        if (port.protocol != "serial") {
            continue;
        }
        any = true;
        const auto* known = s2a::BoardDatabase::builtin().find(port.boardId);
        std::cout << port.address << "\t" << (known != nullptr ? known->name : tr("cli.unknown_board_short")) << "\t"
                  << port.protocolLabel << '\n';
    }
    if (!any) {
        print("cli.no_ports");
    }
    return kSuccess;
}

int Cli::setup() const {
    const auto cli = arduinoCli();
    if (!cli) {
        return kFailure;
    }
    std::vector<const s2a::Board*> boards;
    if (options_.boardGiven) {
        const auto* selected = board();
        if (selected == nullptr) {
            return kUsage;
        }
        boards.push_back(selected);
    } else {
        std::set<std::string> platforms;
        for (const auto& known : s2a::BoardDatabase::builtin().boards()) {
            if (platforms.insert(known.platform).second) {
                boards.push_back(&known);
            }
        }
    }
    for (const auto* selected : boards) {
        if (const int status = ensurePlatform(*cli, *selected); status != kSuccess) {
            return status;
        }
    }
    print("cli.ready");
    return kSuccess;
}

int Cli::compileCommand() const {
    if (options_.positional.size() != 1) {
        print("cli.usage");
        return kUsage;
    }
    const auto* selected = board();
    const auto cli = arduinoCli();
    if (selected == nullptr || !cli) {
        return kFailure;
    }
    const fs::path sketch = sketchPath(options_.positional[0]);
    const fs::path build = buildDirectory(sketch, *selected);
    return compile(*cli, *selected, sketch, build);
}

int Cli::uploadCommand() const {
    if (options_.positional.size() != 1 || options_.port.empty()) {
        print("cli.usage");
        return kUsage;
    }
    const auto* selected = board();
    const auto cli = arduinoCli();
    if (selected == nullptr || !cli) {
        return kFailure;
    }
    const fs::path sketch = sketchPath(options_.positional[0]);
    const fs::path build = buildDirectory(sketch, *selected);
    if (const int status = compile(*cli, *selected, sketch, build); status != kSuccess) {
        return status;
    }
    print("cli.uploading", {{"port", options_.port}});
    const auto outcome = cli->upload(sketch, *selected, build, options_.port);
    if (!outcome.ok()) {
        std::cerr << tr(s2a::errorMessageKey(outcome.error().code)) << '\n';
        return kFailure;
    }
    if (const auto diagnosis = s2a::toolchain::diagnoseUpload(outcome.value(), options_.port);
        diagnosis.problem != s2a::toolchain::Problem::None) {
        return report(diagnosis);
    }
    print("cli.uploaded");
    return kSuccess;
}

int Cli::version() const {
    std::cout << s2a::version::kAppName << ' ' << s2a::version::kString << '\n';
    const fs::path executable = s2a::toolchain::locateArduinoCli();
    if (executable.empty()) {
        fail("cli.arduino_cli_missing");
        return kFailure;
    }
    const ArduinoCli cli(executable, s2a::toolchain::Directories::defaults());
    const auto version = cli.version();
    std::cout << "arduino-cli " << (version.ok() ? version.value() : "?") << " (" << executable.u8string() << ")\n";
    return version.ok() ? kSuccess : kFailure;
}

int Cli::run() {
    if (options_.command == "boards") {
        return boards();
    }
    if (options_.command == "ports") {
        return ports();
    }
    if (options_.command == "setup") {
        return setup();
    }
    if (options_.command == "compile") {
        return compileCommand();
    }
    if (options_.command == "upload") {
        return uploadCommand();
    }
    if (options_.command == "version" || options_.command == "--version") {
        return version();
    }
    print("cli.usage");
    return options_.command.empty() || options_.command == "help" || options_.command == "--help" ? kSuccess : kUsage;
}

std::optional<Options> parse(const std::vector<std::string>& args) {
    Options options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto value = [&]() -> std::optional<std::string> {
            return i + 1 < args.size() ? std::optional(args[++i]) : std::nullopt;
        };
        if (arg == "--board" || arg == "-b") {
            const auto board = value();
            if (!board) {
                return std::nullopt;
            }
            options.boardId = *board;
            options.boardGiven = true;
        } else if (arg == "--port" || arg == "-p") {
            const auto port = value();
            if (!port) {
                return std::nullopt;
            }
            options.port = *port;
        } else if (arg == "--lang") {
            const auto language = value();
            if (!language) {
                return std::nullopt;
            }
            options.language = s2a::languageFromLocale(*language);
        } else if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (options.command.empty()) {
            options.command = arg;
        } else {
            options.positional.push_back(arg);
        }
    }
    return options;
}

/// Arguments in UTF-8. On Windows argv uses the ANSI code page, so read the UTF-16 command
/// line instead (paths with accents would not be found otherwise).
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
    for (int i = 1; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(size > 1 ? static_cast<std::size_t>(size - 1) : 0U, '\0');
        if (size > 1) {
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), size, nullptr, nullptr);
        }
        result.push_back(std::move(arg));
    }
    LocalFree(static_cast<HLOCAL>(wide));
    return result;
#else
    return {argv + 1, argv + argc};
#endif
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // accents in French messages
#endif
    const std::vector<std::string> args = utf8Arguments(argc, argv);
    auto options = parse(args);
    if (!options) {
        std::cerr << s2a::tr("cli.usage", s2a::detectSystemLanguage()) << '\n';
        return kUsage;
    }
    return Cli(std::move(*options)).run();
}
