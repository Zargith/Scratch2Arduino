#pragma once

#include "s2a/boards.hpp"
#include "s2a/process.hpp"
#include "s2a/project.hpp"
#include "s2a/result.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace s2a::toolchain {

/// Per-user folder of Scratch2Arduino:
/// %LOCALAPPDATA%\Scratch2Arduino, ~/Library/Application Support/Scratch2Arduino or
/// $XDG_DATA_HOME/Scratch2Arduino (~/.local/share/Scratch2Arduino).
[[nodiscard]] std::filesystem::path userDataDirectory();

/// Where arduino-cli keeps its cores, downloads and libraries. Private to Scratch2Arduino, so
/// it never interferes with an Arduino IDE installed on the same computer.
struct Directories {
    std::filesystem::path data;
    std::filesystem::path downloads;
    std::filesystem::path user;

    /// <userDataDirectory()>/arduino/{data,downloads,user}
    [[nodiscard]] static Directories defaults();
};

/// Finds arduino-cli: $S2A_ARDUINO_CLI, next to the running executable (bundled copy),
/// <exe>/../libexec/scratch2arduino (Linux install), then the PATH. Empty if not found.
[[nodiscard]] std::filesystem::path locateArduinoCli();

/// A serial port seen by arduino-cli, with the board we think is plugged in.
struct DetectedPort {
    std::string address;       ///< "/dev/ttyACM0", "COM3"
    std::string label;         ///< Name to show.
    std::string protocol;      ///< "serial", "network"...
    std::string protocolLabel; ///< "Serial Port (USB)"
    std::optional<UsbId> usb;
    std::vector<std::string> matchingFqbns; ///< Boards recognised by arduino-cli.
    std::string boardId; ///< Our board id ("uno"...), empty when unknown.
};

/// Result of an arduino-cli command that ran (successfully or not).
struct CommandOutcome {
    bool success = false;
    bool timedOut = false;
    std::string out;   ///< Tool output for the user (compiler/uploader messages).
    std::string err;   ///< Tool error output.
    std::string error; ///< arduino-cli's own error message, if any.
};

struct MemoryUsage {
    std::string section; ///< "text" (program storage) or "data" (global variables).
    std::uint64_t size = 0;
    std::uint64_t max = 0;
};

struct CompileOutcome : CommandOutcome {
    std::vector<MemoryUsage> memory;
};

/// A library of the Arduino registry (arduino-cli lib search).
struct LibraryInfo {
    std::string name;     ///< Registry name, as given to installLibrary() ("Adafruit NeoPixel").
    std::string author;
    std::string sentence; ///< One-line description (in English, as published).
    std::string category; ///< "Display", "Sensors"...
    std::string website;
    std::string latestVersion;
    std::vector<std::string> versions;      ///< Every published version, oldest first.
    std::vector<std::string> architectures; ///< "avr", "esp32", "*" (any)...
};

/// A library installed for arduino-cli (arduino-cli lib list).
struct InstalledLibrary {
    std::string name;
    std::string version;
    std::string author;
    std::string sentence;
    std::string website;
    std::vector<std::string> includes; ///< Headers it provides ("Servo.h").
    /// Comes with a board package (Wire, SoftwareSerial...): it cannot be removed.
    bool builtIn = false;
};


/// Drives the arduino-cli executable (JSON output).
class ArduinoCli {
public:
    ArduinoCli(std::filesystem::path executable, Directories directories,
               process::Runner runner = process::systemRunner());

    [[nodiscard]] const std::filesystem::path& executable() const noexcept { return executable_; }

    /// arduino-cli version, e.g. "1.5.1".
    [[nodiscard]] Result<std::string> version() const;

    /// True when the platform (core) of a board, e.g. "arduino:avr", is installed.
    [[nodiscard]] Result<bool> isPlatformInstalled(std::string_view platform) const;

    /// Downloads and installs a platform (needs the internet, once per platform).
    [[nodiscard]] Result<CommandOutcome> installPlatform(std::string_view platform) const;

    /// Names of the Arduino libraries installed in the private folder ("Servo"...).
    [[nodiscard]] Result<std::vector<std::string>> installedLibraries() const;

    /// Downloads and installs libraries by name (needs the internet, once per library).
    /// A name may carry a version: "Servo@1.2.0".
    [[nodiscard]] Result<CommandOutcome> installLibraries(const std::vector<std::string>& names) const;

    /// The installed libraries, with their version, those of the board packages included.
    [[nodiscard]] Result<std::vector<InstalledLibrary>> listLibraries() const;

    /// Downloads the list of the libraries of the Arduino registry (needs the internet).
    [[nodiscard]] Result<CommandOutcome> updateLibraryIndex() const;

    /// Libraries of the registry matching `query` (words searched in names, authors and
    /// descriptions), best matches first, at most `limit`. Uses the downloaded list: call
    /// updateLibraryIndex() first when it may be missing or old.
    [[nodiscard]] Result<std::vector<LibraryInfo>> searchLibraries(std::string_view query, std::size_t limit) const;

    /// Installs one library (its latest version when `version` is empty) and the libraries it
    /// needs. Replaces an installed copy of another version.
    [[nodiscard]] Result<CommandOutcome> installLibrary(std::string_view name, std::string_view version) const;

    /// Removes an installed library.
    [[nodiscard]] Result<CommandOutcome> uninstallLibrary(std::string_view name) const;

    /// Serial ports, with the matching board of `boards` when it can be recognised.
    [[nodiscard]] Result<std::vector<DetectedPort>> detectPorts(const BoardDatabase& boards = BoardDatabase::builtin()) const;

    /// Compiles the sketch folder `sketch` (containing <name>/<name>.ino) for `board`.
    [[nodiscard]] Result<CompileOutcome> compile(const std::filesystem::path& sketch, const Board& board,
                                                 const std::filesystem::path& buildDirectory) const;

    /// Uploads a sketch compiled by compile() with the same build directory.
    [[nodiscard]] Result<CommandOutcome> upload(const std::filesystem::path& sketch, const Board& board,
                                                const std::filesystem::path& buildDirectory,
                                                std::string_view port) const;

private:
    [[nodiscard]] Result<process::Output> run(std::vector<std::string> args, std::chrono::milliseconds timeout) const;

    std::filesystem::path executable_;
    Directories directories_;
    process::Runner runner_;
};

/// Arduino libraries (registry names) a sketch includes, for the libraries Scratch2Arduino
/// knows how to install ("#include <Servo.h>" -> "Servo"). Sorted, without duplicates.
[[nodiscard]] std::vector<std::string> requiredLibraries(std::string_view source);

/// The libraries of `required` that are not installed yet.
[[nodiscard]] Result<std::vector<std::string>> missingLibraries(const ArduinoCli& cli, const std::vector<std::string>& required);

/// What to install for a project: the libraries of `required` (names, any version) and of
/// `requests` (Project::libraries) that are not installed, or not in the requested version.
/// Returned as arguments of installLibraries(): "Name" or "Name@1.2.3". `installed` is the
/// result of listLibraries().
[[nodiscard]] std::vector<std::string> librariesToInstall(const std::vector<std::string>& required,
                                                          const std::vector<project::Library>& requests,
                                                          const std::vector<InstalledLibrary>& installed);

/// Registry names worth suggesting for a missing header ("ArduinoJson.h" -> "ArduinoJson"):
/// the libraries named like the header first, then those declaring it. Empty when unknown.
[[nodiscard]] Result<std::vector<std::string>> librariesProviding(const ArduinoCli& cli, std::string_view header);

} // namespace s2a::toolchain
