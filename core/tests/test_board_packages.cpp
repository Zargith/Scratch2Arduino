#include "s2a/board_packages.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;
using s2a::process::Command;
using s2a::process::Output;
using s2a::toolchain::ArduinoCli;
using s2a::toolchain::BoardPackages;
using s2a::toolchain::Directories;
using s2a::toolchain::HostSystem;
using s2a::toolchain::InstalledPlatform;

/// A board index in the form arduino-cli downloads: two versions of a platform, and a tool shared
/// with another platform. Sizes are strings, as in the real indexes.
constexpr std::string_view kIndex = R"j({
  "packages": [{
    "name": "acme",
    "platforms": [
      {"architecture": "rocket", "version": "1.9.0", "archiveFileName": "rocket-1.9.0.zip", "size": "500",
       "toolsDependencies": [{"packager": "acme", "name": "gcc", "version": "1"}]},
      {"architecture": "rocket", "version": "1.10.0", "archiveFileName": "rocket-1.10.0.zip", "size": "1000",
       "toolsDependencies": [{"packager": "acme", "name": "gcc", "version": "2"},
                             {"packager": "acme", "name": "uploader", "version": "1"}]},
      {"architecture": "anvil", "version": "3.0.0", "archiveFileName": "anvil-3.0.0.zip", "size": "70",
       "toolsDependencies": [{"packager": "acme", "name": "uploader", "version": "1"}]}
    ],
    "tools": [
      {"name": "gcc", "version": "1", "systems": [
        {"host": "x86_64-pc-linux-gnu", "archiveFileName": "gcc-1-linux.tar.bz2", "size": "40"}]},
      {"name": "gcc", "version": "2", "systems": [
        {"host": "x86_64-pc-linux-gnu", "archiveFileName": "gcc-2-linux.tar.bz2", "size": "200"},
        {"host": "i686-mingw32", "archiveFileName": "gcc-2-win32.zip", "size": "300"},
        {"host": "x86_64-apple-darwin", "archiveFileName": "gcc-2-mac.tar.bz2", "size": "250"}]},
      {"name": "uploader", "version": "1", "systems": [
        {"host": "x86_64-pc-linux-gnu", "archiveFileName": "uploader-linux.tar.gz", "size": "20"},
        {"host": "x86_64-mingw32", "archiveFileName": "uploader-win64.zip", "size": "25"},
        {"host": "i686-mingw32", "archiveFileName": "uploader-win32.zip", "size": "22"},
        {"host": "i386-apple-darwin11", "archiveFileName": "uploader-mac.tar.gz", "size": "21"}]}
    ]
  }]
})j";

struct Folders {
    fs::path root;
    Directories directories;

    explicit Folders(const std::string& name) : root(fs::temp_directory_path() / name) {
        fs::remove_all(root);
        directories = {root / "data", root / "downloads", root / "user"};
        fs::create_directories(directories.data);
        fs::create_directories(directories.downloads / "packages");
        write(directories.data / "package_acme_index.json", std::string(kIndex));
    }
    ~Folders() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    Folders(const Folders&) = delete;
    Folders& operator=(const Folders&) = delete;

    static void write(const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    void download(const std::string& name, std::size_t bytes = 1) const {
        write(directories.downloads / "packages" / name, std::string(bytes, 'x'));
    }
};

TEST(BoardPackages, EveryBoardBelongsToOnePackage) {
    const auto& packages = BoardPackages::builtin();
    ASSERT_FALSE(packages.packages().empty()) << "resources/board-packages.json did not load";
    for (const auto& board : s2a::BoardDatabase::builtin().boards()) {
        const auto* package = packages.forBoard(board);
        ASSERT_NE(package, nullptr) << board.id;
        EXPECT_NE(std::find(package->boards.begin(), package->boards.end(), board.id), package->boards.end());
    }
}

TEST(BoardPackages, OnlyArduinoAvrIsShipped) {
    const auto& packages = BoardPackages::builtin();
    for (const auto& package : packages.packages()) {
        EXPECT_EQ(package.shipped, package.platform == "arduino:avr") << package.id;
        EXPECT_FALSE(package.nameFr.empty());
        EXPECT_FALSE(package.nameEn.empty());
        EXPECT_FALSE(package.boards.empty()) << package.id;
        if (!package.shipped) {
            EXPECT_FALSE(package.version.empty()) << package.id << ": a downloaded package has a pinned version";
            EXPECT_GT(package.downloadBytes, 0U) << package.id;
            EXPECT_GE(package.diskBytes, package.downloadBytes) << package.id;
        }
    }
    const auto* esp32 = packages.find("esp32");
    ASSERT_NE(esp32, nullptr);
    EXPECT_FALSE(esp32->shipped);
    EXPECT_EQ(esp32->boards, std::vector<std::string>{"esp32"});
}

TEST(BoardPackages, AdditionalIndexesComeFromThePackages) {
    const std::string urls = BoardPackages::builtin().additionalIndexUrls();
    EXPECT_NE(urls.find("package_esp32_index.json"), std::string::npos);
    EXPECT_NE(urls.find("package_esp8266com_index.json"), std::string::npos);
    EXPECT_EQ(urls.find("  "), std::string::npos);
}

TEST(BoardPackages, RejectsABoardWithoutPackage) {
    const auto result = BoardPackages::fromJson(
        R"j({"packages": [{"id": "avr", "name": {"fr": "a", "en": "a"}, "platform": "arduino:avr", "shipped": true}]})j",
        s2a::BoardDatabase::builtin());
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().detail.find("esp32"), std::string::npos);
}

TEST(BoardPackages, NeedsExactlyOneShippedPackage) {
    const auto result = BoardPackages::fromJson(
        R"j({"packages": [{"id": "a", "name": {"fr": "a", "en": "a"}, "platform": "x:y"}]})j", s2a::BoardDatabase{});
    EXPECT_FALSE(result.ok());
}

TEST(BoardPackages, FormatsSizesForPeople) {
    using s2a::Language;
    using s2a::toolchain::formatSize;
    EXPECT_EQ(formatSize(1'940'000'000, Language::French), "1,9 Go");
    EXPECT_EQ(formatSize(1'940'000'000, Language::English), "1.9 GB");
    EXPECT_EQ(formatSize(8'000'000'000, Language::English), "8 GB");
    EXPECT_EQ(formatSize(12'400'000'000, Language::English), "12 GB");
    EXPECT_EQ(formatSize(125'000'000, Language::French), "125 Mo");
    EXPECT_EQ(formatSize(1'500'000, Language::English), "1.5 MB");
    EXPECT_EQ(formatSize(1'200, Language::French), "2 ko");
}

TEST(BoardPackages, DownloadSizeCountsThePlatformAndItsToolsForTheHost) {
    const Folders folders("s2a-packages-size-test");
    // Latest version (1.10.0 > 1.9.0): 1000 + gcc 2 + uploader.
    EXPECT_EQ(downloadSize(folders.directories, "acme:rocket", "", HostSystem::LinuxX64), 1000U + 200U + 20U);
    // No 64-bit Windows gcc: the 32-bit one, as arduino-cli does.
    EXPECT_EQ(downloadSize(folders.directories, "acme:rocket", "", HostSystem::WindowsX64), 1000U + 300U + 25U);
    // Apple Silicon takes the Intel builds; a host with a version number matches.
    EXPECT_EQ(downloadSize(folders.directories, "acme:rocket", "", HostSystem::MacArm64), 1000U + 250U + 21U);
    EXPECT_EQ(downloadSize(folders.directories, "acme:rocket", "1.9.0", HostSystem::LinuxX64), 500U + 40U);
    EXPECT_FALSE(downloadSize(folders.directories, "acme:rocket", "9.9.9", HostSystem::LinuxX64).has_value());
    EXPECT_FALSE(downloadSize(folders.directories, "other:board", "", HostSystem::LinuxX64).has_value());
}

TEST(BoardPackages, DownloadSizeLeavesOutWhatIsAlreadyThere) {
    const Folders folders("s2a-packages-size-cached-test");
    folders.download("rocket-1.10.0.zip");                                               // archive kept
    fs::create_directories(folders.directories.data / "packages/acme/tools/uploader/1"); // tool installed
    EXPECT_EQ(downloadSize(folders.directories, "acme:rocket", "1.10.0", HostSystem::LinuxX64), 200U);
}

TEST(BoardPackages, DiskUsageAddsTheFoldersAndArchives) {
    const Folders folders("s2a-packages-disk-test");
    Folders::write(folders.directories.data / "packages/acme/hardware/rocket/1.10.0/platform.txt", std::string(100, 'p'));
    Folders::write(folders.directories.data / "packages/acme/tools/gcc/2/bin/gcc", std::string(300, 'g'));
    Folders::write(folders.directories.data / "packages/acme/tools/gcc/1/bin/gcc", std::string(999, 'o')); // other version
    folders.download("rocket-1.10.0.zip", 50);
    EXPECT_EQ(diskUsage(folders.directories, "acme:rocket", "1.10.0"), 100U + 300U + 50U);
}

TEST(BoardPackages, ArchivesOfARemovedPlatformSkipThoseStillNeeded) {
    const Folders folders("s2a-packages-archives-test");
    for (const char* name : {"rocket-1.9.0.zip", "rocket-1.10.0.zip", "gcc-1-linux.tar.bz2", "gcc-2-linux.tar.bz2",
                             "uploader-linux.tar.gz", "anvil-3.0.0.zip", "unrelated.zip"}) {
        folders.download(name);
    }
    const std::vector<InstalledPlatform> kept{{"acme:anvil", "3.0.0"}};
    std::set<std::string> names;
    for (const fs::path& archive : archivesOnlyUsedBy(folders.directories, "acme:rocket", kept)) {
        names.insert(archive.filename().string());
    }
    // Every version of the platform and its tools go; the uploader stays (the anvil needs it).
    EXPECT_EQ(names, (std::set<std::string>{"rocket-1.9.0.zip", "rocket-1.10.0.zip", "gcc-1-linux.tar.bz2",
                                            "gcc-2-linux.tar.bz2"}));
}

/// Records the commands and replies with canned outputs, in order.
struct FakeRunner {
    std::vector<Command> commands;
    std::vector<Output> replies;

    s2a::process::Runner runner() {
        return [this](const Command& command) -> s2a::Result<Output> {
            commands.push_back(command);
            if (replies.empty()) {
                return s2a::Error{s2a::ErrorCode::ProcessFailed, "no reply"};
            }
            Output reply = replies.front();
            replies.erase(replies.begin());
            return reply;
        };
    }
};

Output reply(std::string out) {
    Output output;
    output.out = std::move(out);
    output.exitCode = 0;
    return output;
}

constexpr std::string_view kCoreList = R"j({"platforms": [
  {"id": "arduino:avr", "installed_version": "1.8.6", "latest_version": "1.8.6"},
  {"id": "esp32:esp32", "installed_version": "3.0.0", "latest_version": "3.3.12"},
  {"id": "acme:rocket", "latest_version": "1.0.0"}
]})j";

TEST(BoardPackages, ListsInstalledPlatforms) {
    FakeRunner fake;
    fake.replies = {reply(std::string(kCoreList))};
    const ArduinoCli cli("arduino-cli", {"/data", "/downloads", "/user"}, fake.runner());
    const auto platforms = cli.installedPlatforms();
    ASSERT_TRUE(platforms.ok());
    ASSERT_EQ(platforms.value().size(), 2U); // no installed_version: not installed
    EXPECT_EQ(platforms.value()[1].id, "esp32:esp32");
    EXPECT_EQ(platforms.value()[1].version, "3.0.0");
}

TEST(BoardPackages, StatesTellWhatIsInstalledAndWhatToDownload) {
    const Folders folders("s2a-packages-states-test");
    FakeRunner fake;
    fake.replies = {reply(std::string(kCoreList))};
    const ArduinoCli cli("arduino-cli", folders.directories, fake.runner());
    const auto states = packageStates(cli);
    ASSERT_TRUE(states.ok());
    ASSERT_EQ(states.value().size(), BoardPackages::builtin().packages().size());
    for (const auto& state : states.value()) {
        if (state.package->id == "arduino-avr") {
            EXPECT_EQ(state.installedVersion, "1.8.6");
            EXPECT_EQ(state.downloadBytes, 0U); // shipped
        } else if (state.package->id == "esp32") {
            EXPECT_EQ(state.installedVersion, "3.0.0");
            // Another version than the pinned one: updating downloads (the estimate: no index here).
            EXPECT_EQ(state.downloadBytes, state.package->downloadBytes);
        } else if (state.package->id == "esp8266") {
            EXPECT_EQ(state.installedVersion, "");
            EXPECT_EQ(state.downloadBytes, state.package->downloadBytes);
            EXPECT_EQ(state.diskBytes, state.package->diskBytes);
        }
    }
}

TEST(BoardPackages, InstallsThePinnedVersion) {
    const Folders folders("s2a-packages-install-test");
    FakeRunner fake;
    fake.replies = {reply("{}"), reply("{}")};
    const ArduinoCli cli(folders.root / "bin" / "arduino-cli", folders.directories, fake.runner());
    const auto* esp32 = BoardPackages::builtin().find("esp32");
    ASSERT_NE(esp32, nullptr);
    const auto outcome = installBoardPackage(cli, *esp32);
    ASSERT_TRUE(outcome.ok());
    ASSERT_EQ(fake.commands.size(), 2U);
    EXPECT_EQ(fake.commands[0].args.at(1), "update-index");
    EXPECT_EQ(fake.commands[1].args.at(2), "esp32:esp32@" + esp32->version);
}

TEST(BoardPackages, RemovesAPackageWithArduinoCli) {
    const Folders folders("s2a-packages-remove-test");
    FakeRunner fake;
    fake.replies = {reply("{}"), reply(R"j({"platforms": []})j")};
    const ArduinoCli cli("arduino-cli", folders.directories, fake.runner());
    const auto* esp32 = BoardPackages::builtin().find("esp32");
    ASSERT_NE(esp32, nullptr);
    const auto outcome = removeBoardPackage(cli, *esp32);
    ASSERT_TRUE(outcome.ok());
    EXPECT_TRUE(outcome.value().success);
    ASSERT_EQ(fake.commands.size(), 2U);
    EXPECT_EQ(fake.commands[0].args.at(1), "uninstall");
    EXPECT_EQ(fake.commands[0].args.at(2), "esp32:esp32");
}

TEST(BoardPackages, TheShippedPackageCannotBeRemoved) {
    FakeRunner fake;
    const ArduinoCli cli("arduino-cli", {"/data", "/downloads", "/user"}, fake.runner());
    const auto* avr = BoardPackages::builtin().find("arduino-avr");
    ASSERT_NE(avr, nullptr);
    EXPECT_FALSE(removeBoardPackage(cli, *avr).ok());
    EXPECT_TRUE(fake.commands.empty());
}

} // namespace
