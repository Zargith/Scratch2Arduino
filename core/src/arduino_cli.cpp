#include "s2a/arduino_cli.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <nlohmann/json.hpp>

namespace s2a::toolchain {

namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

constexpr auto kQuickTimeout = 60s;
constexpr auto kCompileTimeout = 10min;
constexpr auto kUploadTimeout = 3min;
constexpr auto kInstallTimeout = 30min;

fs::path environmentPath(const char* name) {
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe)
    return value != nullptr && *value != '\0' ? fs::u8path(value) : fs::path{};
}

std::string stringOr(const Json& node, const char* key) {
    return node.contains(key) && node.at(key).is_string() ? node.at(key).get<std::string>() : std::string{};
}

/// arduino-cli prints JSON even on failure; this keeps going when it does not.
Json parseOutput(const std::string& text) {
    Json json = Json::parse(text, nullptr, /*allow_exceptions=*/false);
    return json.is_object() ? json : Json::object();
}

std::optional<std::uint16_t> parseHex(const std::string& text) {
    try {
        std::size_t used = 0;
        const unsigned long value = std::stoul(text, &used, 16);
        if (used == text.size() && value <= 0xFFFF) {
            return static_cast<std::uint16_t>(value);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::string identifyBoard(const DetectedPort& port, const BoardDatabase& boards) {
    // 1. A board recognised by arduino-cli whose FQBN is one of ours (options ignored).
    for (const auto& fqbn : port.matchingFqbns) {
        for (const Board& board : boards.boards()) {
            const std::string& ours = board.fqbn;
            if (fqbn == ours || ours.rfind(fqbn + ":", 0) == 0) {
                return board.id;
            }
        }
    }
    // 2. The USB identifiers (generic USB-serial chips may match several boards: take the first).
    if (port.usb) {
        const auto matches = boards.matchUsb(*port.usb);
        if (!matches.empty()) {
            return matches.front()->id;
        }
    }
    return {};
}

/// Outcome of a command printing JSON. arduino-cli 1.5.1 writes its result to stdout, but a
/// fatal error ({"error": ...}) to stderr; uploader messages are at the root on success and
/// under "output" on failure.
CommandOutcome commandOutcome(const process::Output& output) {
    CommandOutcome outcome;
    outcome.timedOut = output.timedOut;
    outcome.success = output.succeeded();
    const Json out = parseOutput(output.out);
    const Json err = parseOutput(output.err);
    const Json& json = out.empty() ? err : out;
    outcome.error = stringOr(json, "error");
    if (outcome.error.empty()) {
        outcome.error = stringOr(err, "error");
    }
    const Json& streams = json.contains("output") && json.at("output").is_object() ? json.at("output") : json;
    outcome.out = stringOr(streams, "stdout");
    outcome.err = stringOr(streams, "stderr");
    if (out.empty() && err.empty()) {
        // Not JSON (crash, very old arduino-cli): keep the raw streams.
        outcome.out = output.out;
        outcome.err = output.err;
    }
    if (outcome.error.empty() && !outcome.success) {
        outcome.error = err.empty() ? output.err : outcome.err;
    }
    return outcome;
}

} // namespace

fs::path userDataDirectory() {
#ifdef _WIN32
    fs::path base = environmentPath("LOCALAPPDATA");
    if (base.empty()) {
        base = environmentPath("APPDATA");
    }
#elif defined(__APPLE__)
    fs::path base = environmentPath("HOME");
    if (!base.empty()) {
        base /= "Library/Application Support";
    }
#else
    fs::path base = environmentPath("XDG_DATA_HOME");
    if (base.empty() && !environmentPath("HOME").empty()) {
        base = environmentPath("HOME") / ".local/share";
    }
#endif
    if (base.empty()) {
        base = fs::temp_directory_path();
    }
    return base / "Scratch2Arduino";
}

Directories Directories::defaults() {
    const fs::path root = userDataDirectory() / "arduino";
    return {root / "data", root / "downloads", root / "user"};
}

fs::path locateArduinoCli() {
#ifdef _WIN32
    const std::string name = "arduino-cli.exe";
#else
    const std::string name = "arduino-cli";
#endif
    std::error_code error;
    if (const fs::path configured = environmentPath("S2A_ARDUINO_CLI");
        !configured.empty() && fs::is_regular_file(configured, error)) {
        return configured;
    }
    if (const fs::path directory = process::executableDirectory(); !directory.empty()) {
        for (const fs::path& candidate :
             {directory / name, directory.parent_path() / "libexec" / "scratch2arduino" / name}) {
            if (fs::is_regular_file(candidate, error)) {
                return candidate;
            }
        }
    }
    return process::findInPath("arduino-cli");
}

ArduinoCli::ArduinoCli(fs::path executable, Directories directories, process::Runner runner)
    : executable_(std::move(executable)), directories_(std::move(directories)), runner_(std::move(runner)) {}

namespace {
constexpr std::string_view kAdditionalBoardIndexes =
    "https://espressif.github.io/arduino-esp32/package_esp32_index.json";
} // namespace

Result<process::Output> ArduinoCli::run(std::vector<std::string> args, std::chrono::milliseconds timeout) const {
    process::Command command;
    command.program = executable_;
    command.args = std::move(args);
    command.args.emplace_back("--json");
    command.args.emplace_back("--no-color");
    command.env = {
        {"ARDUINO_DIRECTORIES_DATA", directories_.data.u8string()},
        {"ARDUINO_DIRECTORIES_DOWNLOADS", directories_.downloads.u8string()},
        {"ARDUINO_DIRECTORIES_USER", directories_.user.u8string()},
        {"ARDUINO_UPDATER_ENABLE_NOTIFICATION", "false"},
        // Where arduino-cli finds the boards that Arduino does not publish (ESP32).
        {"ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS", std::string(kAdditionalBoardIndexes)},
    };
    command.timeout = timeout;
    return runner_(command);
}

Result<std::string> ArduinoCli::version() const {
    auto output = run({"version"}, kQuickTimeout);
    if (!output.ok()) {
        return output.error();
    }
    const Json json = parseOutput(output.value().out);
    const std::string version = stringOr(json, "VersionString");
    if (!output.value().succeeded() || version.empty()) {
        return Error{ErrorCode::ProcessFailed, "arduino-cli version failed: " + output.value().err};
    }
    return version;
}

Result<bool> ArduinoCli::isPlatformInstalled(std::string_view platform) const {
    auto output = run({"core", "list"}, kQuickTimeout);
    if (!output.ok()) {
        return output.error();
    }
    if (!output.value().succeeded()) {
        return Error{ErrorCode::ProcessFailed, "arduino-cli core list failed: " + output.value().err};
    }
    const Json json = parseOutput(output.value().out);
    if (json.contains("platforms") && json.at("platforms").is_array()) {
        for (const Json& entry : json.at("platforms")) {
            if (stringOr(entry, "id") == platform && !stringOr(entry, "installed_version").empty()) {
                return true;
            }
        }
    }
    return false;
}

Result<CommandOutcome> ArduinoCli::installPlatform(std::string_view platform) const {
    auto index = run({"core", "update-index"}, kInstallTimeout);
    if (!index.ok()) {
        return index.error();
    }
    if (!index.value().succeeded()) {
        return commandOutcome(index.value());
    }
    auto install = run({"core", "install", std::string(platform)}, kInstallTimeout);
    if (!install.ok()) {
        return install.error();
    }
    return commandOutcome(install.value());
}

Result<std::vector<std::string>> ArduinoCli::installedLibraries() const {
    auto output = run({"lib", "list"}, kQuickTimeout);
    if (!output.ok()) {
        return output.error();
    }
    if (!output.value().succeeded()) {
        return Error{ErrorCode::ProcessFailed, "arduino-cli lib list failed: " + output.value().err};
    }
    std::vector<std::string> names;
    const Json json = parseOutput(output.value().out);
    if (json.contains("installed_libraries") && json.at("installed_libraries").is_array()) {
        for (const Json& entry : json.at("installed_libraries")) {
            if (entry.contains("library") && entry.at("library").is_object()) {
                if (std::string name = stringOr(entry.at("library"), "name"); !name.empty()) {
                    names.push_back(std::move(name));
                }
            }
        }
    }
    return names;
}

Result<CommandOutcome> ArduinoCli::installLibraries(const std::vector<std::string>& names) const {
    auto index = run({"lib", "update-index"}, kInstallTimeout);
    if (!index.ok()) {
        return index.error();
    }
    if (!index.value().succeeded()) {
        return commandOutcome(index.value());
    }
    std::vector<std::string> args{"lib", "install"};
    args.insert(args.end(), names.begin(), names.end());
    auto install = run(std::move(args), kInstallTimeout);
    if (!install.ok()) {
        return install.error();
    }
    return commandOutcome(install.value());
}

std::vector<std::string> requiredLibraries(std::string_view source) {
    // Header -> library of the Arduino registry. Add a line here for each component library.
    static const std::pair<std::string_view, std::string_view> kKnown[] = {
        {"Servo.h", "Servo"},
        {"ESP32Servo.h", "ESP32Servo"},
        {"DHT.h", "DHT sensor library"},
        {"LiquidCrystal_I2C.h", "LiquidCrystal I2C"},
        {"LiquidCrystal.h", "LiquidCrystal"},
        {"U8x8lib.h", "U8g2"},
        {"U8g2lib.h", "U8g2"},
        {"Adafruit_NeoPixel.h", "Adafruit NeoPixel"},
    };
    std::set<std::string> found;
    std::size_t position = 0;
    while ((position = source.find("#include", position)) != std::string_view::npos) {
        const std::size_t end = source.find('\n', position);
        const std::string_view line = source.substr(position, end == std::string_view::npos ? end : end - position);
        position += 8;
        const std::size_t open = line.find_first_of("<\"");
        const std::size_t close = open == std::string_view::npos ? open : line.find_first_of(">\"", open + 1);
        if (close == std::string_view::npos) {
            continue;
        }
        const std::string_view header = line.substr(open + 1, close - open - 1);
        for (const auto& [known, library] : kKnown) {
            if (header == known) {
                found.emplace(library);
            }
        }
    }
    return {found.begin(), found.end()};
}

Result<std::vector<std::string>> missingLibraries(const ArduinoCli& cli, const std::vector<std::string>& required) {
    if (required.empty()) {
        return std::vector<std::string>{};
    }
    auto installed = cli.installedLibraries();
    if (!installed.ok()) {
        return installed.error();
    }
    std::vector<std::string> missing;
    for (const std::string& name : required) {
        if (std::find(installed.value().begin(), installed.value().end(), name) == installed.value().end()) {
            missing.push_back(name);
        }
    }
    return missing;
}

Result<std::vector<DetectedPort>> ArduinoCli::detectPorts(const BoardDatabase& boards) const {
    auto output = run({"board", "list"}, kQuickTimeout);
    if (!output.ok()) {
        return output.error();
    }
    if (!output.value().succeeded()) {
        return Error{ErrorCode::ProcessFailed, "arduino-cli board list failed: " + output.value().err};
    }
    const Json json = parseOutput(output.value().out);
    std::vector<DetectedPort> ports;
    if (!json.contains("detected_ports") || !json.at("detected_ports").is_array()) {
        return ports;
    }
    for (const Json& entry : json.at("detected_ports")) {
        if (!entry.contains("port") || !entry.at("port").is_object()) {
            continue;
        }
        const Json& portNode = entry.at("port");
        DetectedPort port;
        port.address = stringOr(portNode, "address");
        port.label = stringOr(portNode, "label");
        port.protocol = stringOr(portNode, "protocol");
        port.protocolLabel = stringOr(portNode, "protocol_label");
        if (portNode.contains("properties") && portNode.at("properties").is_object()) {
            const auto vendor = parseHex(stringOr(portNode.at("properties"), "vid"));
            const auto product = parseHex(stringOr(portNode.at("properties"), "pid"));
            if (vendor && product) {
                port.usb = UsbId{*vendor, *product};
            }
        }
        if (entry.contains("matching_boards") && entry.at("matching_boards").is_array()) {
            for (const Json& board : entry.at("matching_boards")) {
                if (const std::string fqbn = stringOr(board, "fqbn"); !fqbn.empty()) {
                    port.matchingFqbns.push_back(fqbn);
                }
            }
        }
        if (port.address.empty()) {
            continue;
        }
        if (port.label.empty()) {
            port.label = port.address;
        }
        port.boardId = identifyBoard(port, boards);
        ports.push_back(std::move(port));
    }
    return ports;
}

Result<CompileOutcome> ArduinoCli::compile(const fs::path& sketch, const Board& board,
                                           const fs::path& buildDirectory) const {
    auto output = run({"compile", "--fqbn", board.fqbn, "--build-path", buildDirectory.u8string(), sketch.u8string()},
                      kCompileTimeout);
    if (!output.ok()) {
        return output.error();
    }
    const Json json = parseOutput(output.value().out);
    CompileOutcome outcome;
    static_cast<CommandOutcome&>(outcome) = commandOutcome(output.value());
    outcome.out = stringOr(json, "compiler_out");
    outcome.err = stringOr(json, "compiler_err");
    if (json.empty()) {
        outcome.out = output.value().out;
        outcome.err = output.value().err;
    }
    if (json.contains("success") && json.at("success").is_boolean()) {
        outcome.success = outcome.success && json.at("success").get<bool>();
    }
    if (json.contains("builder_result") && json.at("builder_result").contains("executable_sections_size") &&
        json.at("builder_result").at("executable_sections_size").is_array()) {
        for (const Json& section : json.at("builder_result").at("executable_sections_size")) {
            outcome.memory.push_back({stringOr(section, "name"), section.value("size", std::uint64_t{0}),
                                      section.value("max_size", std::uint64_t{0})});
        }
    }
    return outcome;
}

Result<CommandOutcome> ArduinoCli::upload(const fs::path& sketch, const Board& board, const fs::path& buildDirectory,
                                          std::string_view port) const {
    auto output = run({"upload", "--fqbn", board.fqbn, "--port", std::string(port), "--input-dir",
                       buildDirectory.u8string(), sketch.u8string()},
                      kUploadTimeout);
    if (!output.ok()) {
        return output.error();
    }
    return commandOutcome(output.value());
}

} // namespace s2a::toolchain
