#include "s2a/arduino_cli.hpp"

#include "s2a/board_packages.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
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

bool isSystemSerialPort(std::string_view address) {
    // Ports macOS always has (debug console, Bluetooth and Wi-Fi debugging), as /dev/cu.* and /dev/tty.*.
    for (const std::string_view prefix : {std::string_view("/dev/cu."), std::string_view("/dev/tty.")}) {
        if (address.rfind(prefix, 0) == 0) {
            const std::string_view name = address.substr(prefix.size());
            for (const std::string_view system :
                 {"debug-console", "Bluetooth-Incoming-Port", "wlan-debug", "BLTH", "Bluetooth-Modem"}) {
                if (name == system) {
                    return true;
                }
            }
        }
    }
    return false;
}

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
        // Where arduino-cli finds the boards that Arduino does not publish (ESP32, ESP8266, Pico).
        {"ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS", BoardPackages::builtin().additionalIndexUrls()},
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

Result<std::vector<InstalledPlatform>> ArduinoCli::installedPlatforms() const {
    auto output = run({"core", "list"}, kQuickTimeout);
    if (!output.ok()) {
        return output.error();
    }
    if (!output.value().succeeded()) {
        return Error{ErrorCode::ProcessFailed, "arduino-cli core list failed: " + output.value().err};
    }
    std::vector<InstalledPlatform> platforms;
    const Json json = parseOutput(output.value().out);
    if (json.contains("platforms") && json.at("platforms").is_array()) {
        for (const Json& entry : json.at("platforms")) {
            InstalledPlatform platform{stringOr(entry, "id"), stringOr(entry, "installed_version")};
            if (!platform.id.empty() && !platform.version.empty()) {
                platforms.push_back(std::move(platform));
            }
        }
    }
    return platforms;
}

Result<bool> ArduinoCli::isPlatformInstalled(std::string_view platform) const {
    auto platforms = installedPlatforms();
    if (!platforms.ok()) {
        return platforms.error();
    }
    return std::any_of(platforms.value().begin(), platforms.value().end(),
                       [&](const InstalledPlatform& installed) { return installed.id == platform; });
}

namespace {

/// Copies the files of `from` that `to` does not have yet. Errors are ignored: the seed only
/// saves a download, arduino-cli downloads whatever is still missing.
void copyMissingFiles(const fs::path& from, const fs::path& to) {
    std::error_code error;
    if (!fs::is_directory(from, error)) {
        return;
    }
    for (fs::recursive_directory_iterator it(from, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) {
            continue;
        }
        const fs::path target = to / fs::relative(it->path(), from, error);
        if (error || fs::exists(target, error)) {
            error.clear();
            continue;
        }
        fs::create_directories(target.parent_path(), error);
        // Copied under a temporary name first: an interrupted copy never looks complete.
        fs::path partial = target;
        partial += ".part";
        error.clear();
        if (fs::copy_file(it->path(), partial, fs::copy_options::overwrite_existing, error) && !error) {
            fs::rename(partial, target, error);
        }
        if (error) {
            fs::remove(partial, error);
        }
        error.clear();
    }
}

} // namespace

void ArduinoCli::seedFromBundle() const {
    const fs::path seed = executable_.parent_path() / "arduino-seed";
    copyMissingFiles(seed / "downloads", directories_.downloads);
    copyMissingFiles(seed / "data", directories_.data);
}

std::string ArduinoCli::seededVersion(std::string_view platform) const {
    // platforms.txt, written when the seed is made: one "vendor:arch@version" per line.
    std::ifstream file(executable_.parent_path() / "arduino-seed" / "platforms.txt");
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
            line.pop_back();
        }
        const auto at = line.find('@');
        if (at != std::string::npos && std::string_view(line).substr(0, at) == platform) {
            return line.substr(at + 1);
        }
    }
    return {};
}

Result<CommandOutcome> ArduinoCli::installPlatform(std::string_view platform, std::string_view version) const {
    seedFromBundle();
    std::error_code error;
    const bool haveIndex = fs::is_regular_file(directories_.data / "package_index.json", error);
    std::string target(platform);
    if (const std::string seeded = seededVersion(platform); !seeded.empty() && haveIndex) {
        // Shipped by the installer: this exact version is in the downloads folder, so nothing is
        // downloaded (a whole classroom on the same Wi-Fi installs in seconds).
        target += "@" + seeded;
    } else {
        if (!version.empty()) {
            target += "@" + std::string(version);
        }
        auto index = run({"core", "update-index"}, kInstallTimeout);
        if (!index.ok()) {
            return index.error();
        }
        // Offline with an index already there: the install may still work from the downloads.
        if (!index.value().succeeded() && !haveIndex) {
            return commandOutcome(index.value());
        }
    }
    auto install = run({"core", "install", target}, kInstallTimeout);
    if (!install.ok()) {
        return install.error();
    }
    return commandOutcome(install.value());
}

Result<CommandOutcome> ArduinoCli::uninstallPlatform(std::string_view platform) const {
    auto output = run({"core", "uninstall", std::string(platform)}, kInstallTimeout);
    if (!output.ok()) {
        return output.error();
    }
    return commandOutcome(output.value());
}

Result<std::vector<std::string>> ArduinoCli::installedLibraries() const {
    auto libraries = listLibraries();
    if (!libraries.ok()) {
        return libraries.error();
    }
    std::vector<std::string> names;
    for (InstalledLibrary& library : libraries.value()) {
        names.push_back(std::move(library.name));
    }
    return names;
}

namespace {

std::vector<std::string> stringsAt(const Json& node, const char* key) {
    std::vector<std::string> values;
    if (node.contains(key) && node.at(key).is_array()) {
        for (const Json& value : node.at(key)) {
            if (value.is_string()) {
                values.push_back(value.get<std::string>());
            }
        }
    }
    return values;
}

/// A registry name or version given by the user: never read as an option of arduino-cli.
bool safeArgument(std::string_view text, std::string_view allowedPunctuation) {
    if (text.empty() || text.size() > 200 || text.front() == '-') {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [&](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return std::isalnum(byte) != 0 || byte >= 0x80 || allowedPunctuation.find(c) != std::string_view::npos;
    });
}

constexpr std::string_view kNamePunctuation = " ._-+()&',/";
constexpr std::string_view kVersionPunctuation = ".-+_";

} // namespace

Result<std::vector<InstalledLibrary>> ArduinoCli::listLibraries() const {
    auto output = run({"lib", "list"}, kQuickTimeout);
    if (!output.ok()) {
        return output.error();
    }
    if (!output.value().succeeded()) {
        return Error{ErrorCode::ProcessFailed, "arduino-cli lib list failed: " + output.value().err};
    }
    std::vector<InstalledLibrary> libraries;
    const Json json = parseOutput(output.value().out);
    if (json.contains("installed_libraries") && json.at("installed_libraries").is_array()) {
        for (const Json& entry : json.at("installed_libraries")) {
            if (!entry.contains("library") || !entry.at("library").is_object()) {
                continue;
            }
            const Json& node = entry.at("library");
            InstalledLibrary library;
            library.name = stringOr(node, "name");
            if (library.name.empty()) {
                continue;
            }
            library.version = stringOr(node, "version");
            library.author = stringOr(node, "author");
            library.sentence = stringOr(node, "sentence");
            library.website = stringOr(node, "website");
            library.includes = stringsAt(node, "provides_includes");
            // "user" for the libraries installed by lib install; "platform", "ide_builtin"...
            const std::string location = stringOr(node, "location");
            library.builtIn = !location.empty() && location != "user";
            libraries.push_back(std::move(library));
        }
    }
    std::sort(libraries.begin(), libraries.end(),
              [](const InstalledLibrary& a, const InstalledLibrary& b) { return a.name < b.name; });
    return libraries;
}

Result<CommandOutcome> ArduinoCli::updateLibraryIndex() const {
    auto index = run({"lib", "update-index"}, kInstallTimeout);
    if (!index.ok()) {
        return index.error();
    }
    return commandOutcome(index.value());
}

Result<std::vector<LibraryInfo>> ArduinoCli::searchLibraries(std::string_view query, std::size_t limit) const {
    std::vector<std::string> args{"lib", "search", "--omit-releases-details"};
    std::string word;
    const auto flush = [&]() {
        // A leading dash would be read as an option.
        const auto start = word.find_first_not_of('-');
        if (start != std::string::npos) {
            args.push_back(word.substr(start));
        }
        word.clear();
    };
    for (const char c : query) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            flush();
        } else {
            word.push_back(c);
        }
    }
    flush();
    auto output = run(std::move(args), kQuickTimeout);
    if (!output.ok()) {
        return output.error();
    }
    if (!output.value().succeeded()) {
        return Error{ErrorCode::ProcessFailed, "arduino-cli lib search failed: " + output.value().err};
    }
    std::vector<LibraryInfo> libraries;
    const Json json = parseOutput(output.value().out);
    if (json.contains("libraries") && json.at("libraries").is_array()) {
        for (const Json& node : json.at("libraries")) {
            if (libraries.size() >= limit) {
                break;
            }
            LibraryInfo library;
            library.name = stringOr(node, "name");
            if (library.name.empty()) {
                continue;
            }
            if (node.contains("latest") && node.at("latest").is_object()) {
                const Json& latest = node.at("latest");
                library.author = stringOr(latest, "author");
                library.sentence = stringOr(latest, "sentence");
                library.category = stringOr(latest, "category");
                library.website = stringOr(latest, "website");
                library.latestVersion = stringOr(latest, "version");
                library.architectures = stringsAt(latest, "architectures");
            }
            library.versions = stringsAt(node, "available_versions");
            libraries.push_back(std::move(library));
        }
    }
    return libraries;
}

Result<CommandOutcome> ArduinoCli::installLibrary(std::string_view name, std::string_view version) const {
    if (!safeArgument(name, kNamePunctuation) || (!version.empty() && !safeArgument(version, kVersionPunctuation))) {
        return Error{ErrorCode::ProcessFailed, "invalid library name or version"};
    }
    std::string argument(name);
    if (!version.empty()) {
        argument += "@" + std::string(version);
    }
    auto install = run({"lib", "install", argument}, kInstallTimeout);
    if (!install.ok()) {
        return install.error();
    }
    return commandOutcome(install.value());
}

Result<CommandOutcome> ArduinoCli::uninstallLibrary(std::string_view name) const {
    if (!safeArgument(name, kNamePunctuation)) {
        return Error{ErrorCode::ProcessFailed, "invalid library name"};
    }
    auto uninstall = run({"lib", "uninstall", std::string(name)}, kQuickTimeout);
    if (!uninstall.ok()) {
        return uninstall.error();
    }
    return commandOutcome(uninstall.value());
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

std::vector<std::string> librariesToInstall(const std::vector<std::string>& required,
                                            const std::vector<project::Library>& requests,
                                            const std::vector<InstalledLibrary>& installed) {
    const auto find = [&](const std::string& name) {
        return std::find_if(installed.begin(), installed.end(),
                            [&](const InstalledLibrary& library) { return library.name == name; });
    };
    std::vector<std::string> install;
    std::set<std::string> seen;
    // The project's own list first: its version wins over "any version".
    for (const project::Library& request : requests) {
        if (request.name.empty() || !seen.insert(request.name).second) {
            continue;
        }
        const auto found = find(request.name);
        if (found == installed.end()) {
            install.push_back(request.version.empty() ? request.name : request.name + "@" + request.version);
        } else if (!request.version.empty() && found->version != request.version && !found->builtIn) {
            install.push_back(request.name + "@" + request.version);
        }
    }
    for (const std::string& name : required) {
        if (seen.insert(name).second && find(name) == installed.end()) {
            install.push_back(name);
        }
    }
    return install;
}

Result<std::vector<std::string>> librariesProviding(const ArduinoCli& cli, std::string_view header) {
    std::string stem(header);
    if (const auto dot = stem.rfind('.'); dot != std::string::npos) {
        stem.erase(dot);
    }
    if (!safeArgument(stem, "._-+")) {
        return std::vector<std::string>{};
    }
    std::vector<std::string> names;
    const auto add = [&](std::string_view query) -> Status {
        auto found = cli.searchLibraries(query, 5);
        if (!found.ok()) {
            return found.error();
        }
        for (LibraryInfo& library : found.value()) {
            if (std::find(names.begin(), names.end(), library.name) == names.end()) {
                names.push_back(std::move(library.name));
            }
        }
        return success();
    };
    if (auto named = add("name=" + stem); !named.ok()) {
        return named.error();
    }
    if (auto provided = add("provides=" + std::string(header)); !provided.ok()) {
        return provided.error();
    }
    return names;
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
        if (port.address.empty() || isSystemSerialPort(port.address)) {
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
        // Read without json::value(), which throws when a field has another type.
        const auto size = [](const Json& section, const char* key) {
            const auto found = section.find(key);
            return found != section.end() && found->is_number_unsigned() ? found->get<std::uint64_t>() : std::uint64_t{0};
        };
        for (const Json& section : json.at("builder_result").at("executable_sections_size")) {
            if (section.is_object()) {
                outcome.memory.push_back({stringOr(section, "name"), size(section, "size"), size(section, "max_size")});
            }
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
