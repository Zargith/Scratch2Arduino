#pragma once

#include "s2a/arduino_cli.hpp"
#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/result.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace s2a::toolchain {

/// A family of boards installed together: one arduino-cli platform (resources/board-packages.json).
/// Only the shipped package (Arduino AVR) comes with the application; the others are downloaded
/// when the user asks for them, in the installer or in the settings of the application.
struct BoardPackage {
    std::string id;       ///< Stable identifier: "esp32", "arduino-renesas".
    std::string nameFr;
    std::string nameEn;
    std::string platform; ///< arduino-cli platform: "esp32:esp32".
    std::string version;  ///< Version installed by the application, empty: the shipped or latest one.
    std::string indexUrl; ///< Board index of a platform that Arduino does not publish, else empty.
    bool shipped = false; ///< Comes with the application, cannot be removed.
    std::uint64_t downloadBytes = 0; ///< Estimated download, used while the index is not there yet.
    std::uint64_t diskBytes = 0;     ///< Estimated space once installed.
    std::vector<std::string> boards; ///< Ids of the boards it provides, in the order of the database.

    [[nodiscard]] const std::string& name(Language language) const {
        return language == Language::English ? nameEn : nameFr;
    }
};

/// The board packages Scratch2Arduino offers.
class BoardPackages {
public:
    /// Parses resources/board-packages.json. Every board of `boards` must belong to one package.
    [[nodiscard]] static Result<BoardPackages> fromJson(std::string_view document, const BoardDatabase& boards);

    /// Packages embedded in the application, with the boards of BoardDatabase::builtin().
    [[nodiscard]] static const BoardPackages& builtin();

    [[nodiscard]] const std::vector<BoardPackage>& packages() const noexcept { return packages_; }
    [[nodiscard]] const BoardPackage* find(std::string_view id) const;
    /// The package providing `board` (null for a board of no package, which the tests forbid).
    [[nodiscard]] const BoardPackage* forBoard(const Board& board) const;
    [[nodiscard]] const BoardPackage* forPlatform(std::string_view platform) const;

    /// Board indexes of the platforms that Arduino does not publish, space-separated (the form
    /// arduino-cli reads from ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS).
    [[nodiscard]] std::string additionalIndexUrls() const;

private:
    std::vector<BoardPackage> packages_;
};

/// Systems the downloads are made for.
enum class HostSystem { WindowsX64, LinuxX64, LinuxArm64, MacArm64, MacX64 };
/// The system running.
[[nodiscard]] HostSystem currentHost();

/// Size of the archives to download for `platform` at `version` (its latest version when empty)
/// on `host`: the platform and the tools it needs, read from the board indexes already downloaded
/// (<data>/package_*index.json). Archives already in the downloads folder are not counted.
/// Nullopt when the indexes do not list the platform.
[[nodiscard]] std::optional<std::uint64_t> downloadSize(const Directories& directories, std::string_view platform,
                                                        std::string_view version, HostSystem host = currentHost());

/// Space used on the disk by `platform` installed at `version`: its folder, the tools it needs and
/// their archives in the downloads folder. Tools shared with another platform are counted too.
[[nodiscard]] std::uint64_t diskUsage(const Directories& directories, std::string_view platform,
                                      std::string_view version);

/// Archives of the downloads folder that only `platform` used (any of its versions): those of the
/// platforms in `kept` are left out. Paths inside directories.downloads.
[[nodiscard]] std::vector<std::filesystem::path> archivesOnlyUsedBy(const Directories& directories,
                                                                    std::string_view platform,
                                                                    const std::vector<InstalledPlatform>& kept);

/// A size for people: "1,9 Go" / "1.9 GB", "350 Mo" / "350 MB" (decimal units, as the systems show).
[[nodiscard]] std::string formatSize(std::uint64_t bytes, Language language);

/// State of a package on this computer.
struct PackageState {
    const BoardPackage* package = nullptr;
    std::string installedVersion;    ///< Empty when not installed.
    std::uint64_t downloadBytes = 0; ///< What installing (or updating) it would download.
    std::uint64_t diskBytes = 0;     ///< Used once installed, or the estimate when not installed.
};

/// Every package with its state, in the order of resources/board-packages.json.
[[nodiscard]] Result<std::vector<PackageState>> packageStates(const ArduinoCli& cli,
                                                              const BoardPackages& packages = BoardPackages::builtin());

/// Installs `package` (its pinned version, or the shipped one).
[[nodiscard]] Result<CommandOutcome> installBoardPackage(const ArduinoCli& cli, const BoardPackage& package);

/// Installs the package providing `board` (its platform at the latest version for a board of no package).
[[nodiscard]] Result<CommandOutcome> installPackageOf(const ArduinoCli& cli, const Board& board,
                                                      const BoardPackages& packages = BoardPackages::builtin());

/// Removes an installed package and the archives only it used. The shipped package cannot be removed.
[[nodiscard]] Result<CommandOutcome> removeBoardPackage(const ArduinoCli& cli, const BoardPackage& package);

} // namespace s2a::toolchain
