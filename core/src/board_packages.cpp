#include "s2a/board_packages.hpp"

#include "s2a/resources.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

namespace s2a::toolchain {

namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

std::string stringAt(const Json& node, const char* key) {
    return node.contains(key) && node.at(key).is_string() ? node.at(key).get<std::string>() : std::string{};
}

std::uint64_t numberAt(const Json& node, const char* key) {
    if (!node.contains(key)) {
        return 0;
    }
    const Json& value = node.at(key);
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_string()) { // the board indexes write sizes as strings
        try {
            return std::stoull(value.get<std::string>());
        } catch (const std::exception&) {
        }
    }
    return 0;
}

/// "vendor:arch" -> {"vendor", "arch"}.
std::pair<std::string, std::string> splitPlatform(std::string_view platform) {
    const auto colon = platform.find(':');
    if (colon == std::string_view::npos) {
        return {std::string(platform), {}};
    }
    return {std::string(platform.substr(0, colon)), std::string(platform.substr(colon + 1))};
}

bool allDigits(const std::string& text) {
    return !text.empty() && text.size() < 10 &&
           std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

/// Compares dotted versions number by number ("1.10.0" > "1.9.2"); other parts compare as text.
bool versionLess(const std::string& a, const std::string& b) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() || j < b.size()) {
        const std::size_t endA = std::min(a.find('.', i), a.size());
        const std::size_t endB = std::min(b.find('.', j), b.size());
        const std::string partA = i < a.size() ? a.substr(i, endA - i) : std::string("0");
        const std::string partB = j < b.size() ? b.substr(j, endB - j) : std::string("0");
        if (allDigits(partA) && allDigits(partB)) {
            if (std::stoul(partA) != std::stoul(partB)) {
                return std::stoul(partA) < std::stoul(partB);
            }
        } else if (partA != partB) {
            return partA < partB;
        }
        i = endA + 1;
        j = endB + 1;
    }
    return false;
}

/// The board indexes downloaded by arduino-cli (<data>/package_*index.json).
class Indexes {
public:
    struct Dependency {
        std::string packager;
        std::string name;
        std::string version;
        const Json* entry = nullptr; ///< Null when no index lists the tool.
    };

    explicit Indexes(const fs::path& data) {
        std::error_code error;
        for (fs::directory_iterator it(data, error), end; !error && it != end; it.increment(error)) {
            const std::string name = it->path().filename().u8string();
            constexpr std::string_view suffix = "index.json";
            if (name.rfind("package_", 0) != 0 || name.size() < suffix.size() ||
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
                continue;
            }
            std::ifstream file(it->path(), std::ios::binary);
            Json json = Json::parse(file, nullptr, /*allow_exceptions=*/false);
            if (json.is_object() && json.contains("packages") && json.at("packages").is_array()) {
                documents_.push_back(std::move(json));
            }
        }
    }

    /// Every version of `platform` listed.
    [[nodiscard]] std::vector<const Json*> platformVersions(std::string_view platform) const {
        const auto [vendor, arch] = splitPlatform(platform);
        std::vector<const Json*> found;
        for (const Json* package : packages(vendor)) {
            for (const Json& entry : arrayAt(*package, "platforms")) {
                if (stringAt(entry, "architecture") == arch) {
                    found.push_back(&entry);
                }
            }
        }
        return found;
    }

    /// `platform` at `version`, or its latest version when `version` is empty.
    [[nodiscard]] const Json* platform(std::string_view platform, std::string_view version) const {
        const Json* best = nullptr;
        for (const Json* entry : platformVersions(platform)) {
            const std::string entryVersion = stringAt(*entry, "version");
            if (!version.empty() ? entryVersion == version
                                 : best == nullptr || versionLess(stringAt(*best, "version"), entryVersion)) {
                best = entry;
            }
        }
        return best;
    }

    /// The tools a platform entry needs.
    [[nodiscard]] std::vector<Dependency> dependencies(const Json& platformEntry) const {
        std::vector<Dependency> found;
        for (const Json& dependency : arrayAt(platformEntry, "toolsDependencies")) {
            Dependency tool{stringAt(dependency, "packager"), stringAt(dependency, "name"),
                            stringAt(dependency, "version"), nullptr};
            tool.entry = this->tool(tool.packager, tool.name, tool.version);
            found.push_back(std::move(tool));
        }
        return found;
    }

private:
    static const Json& arrayAt(const Json& node, const char* key) {
        static const Json empty = Json::array();
        return node.contains(key) && node.at(key).is_array() ? node.at(key) : empty;
    }

    [[nodiscard]] std::vector<const Json*> packages(std::string_view name) const {
        std::vector<const Json*> found;
        for (const Json& document : documents_) {
            for (const Json& package : document.at("packages")) {
                if (stringAt(package, "name") == name) {
                    found.push_back(&package);
                }
            }
        }
        return found;
    }

    [[nodiscard]] const Json* tool(const std::string& packager, const std::string& name,
                                   const std::string& version) const {
        for (const Json* package : packages(packager)) {
            for (const Json& entry : arrayAt(*package, "tools")) {
                if (stringAt(entry, "name") == name && stringAt(entry, "version") == version) {
                    return &entry;
                }
            }
        }
        return nullptr;
    }

    std::vector<Json> documents_;
};

/// Host names of the board indexes for `host`, best first (arduino-cli takes the 32-bit Windows
/// or Intel macOS tools when there is no other build).
std::vector<std::string_view> hostNames(HostSystem host) {
    switch (host) {
    case HostSystem::WindowsX64:
        return {"x86_64-mingw32", "i686-mingw32", "i686-cygwin"};
    case HostSystem::LinuxX64:
        return {"x86_64-pc-linux-gnu", "x86_64-linux-gnu"};
    case HostSystem::LinuxArm64:
        return {"aarch64-linux-gnu", "aarch64-unknown-linux-gnu"};
    case HostSystem::MacArm64:
        return {"arm64-apple-darwin", "x86_64-apple-darwin", "i386-apple-darwin", "i686-apple-darwin"};
    case HostSystem::MacX64:
        return {"x86_64-apple-darwin", "i386-apple-darwin", "i686-apple-darwin"};
    }
    return {};
}

const Json* systemFor(const Json& tool, HostSystem host) {
    if (!tool.contains("systems") || !tool.at("systems").is_array()) {
        return nullptr;
    }
    for (const std::string_view name : hostNames(host)) {
        for (const Json& system : tool.at("systems")) {
            const std::string systemHost = stringAt(system, "host");
            // macOS hosts may carry a version: "i386-apple-darwin11", "arm64-apple-darwin20".
            if (systemHost.rfind(std::string(name), 0) == 0 &&
                (systemHost.size() == name.size() ||
                 std::all_of(systemHost.begin() + static_cast<std::ptrdiff_t>(name.size()), systemHost.end(),
                             [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.'; }))) {
                return &system;
            }
        }
    }
    return nullptr;
}

std::uint64_t directorySize(const fs::path& path) {
    std::error_code error;
    if (fs::is_regular_file(path, error)) {
        const auto size = fs::file_size(path, error);
        return error ? 0 : size;
    }
    std::uint64_t total = 0;
    for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_symlink(error) && it->is_regular_file(error)) {
            const auto size = it->file_size(error);
            total += error ? 0 : size;
        }
        error.clear();
    }
    return total;
}

fs::path archivePath(const Directories& directories, const std::string& fileName) {
    return directories.downloads / "packages" / fs::u8path(fileName);
}

fs::path toolDirectory(const Directories& directories, const Indexes::Dependency& tool) {
    return directories.data / "packages" / fs::u8path(tool.packager) / "tools" / fs::u8path(tool.name) /
           fs::u8path(tool.version);
}

/// Archive names of every system of `tool` (any of them could be in the downloads folder).
void addToolArchives(const Json& tool, std::set<std::string>& names) {
    if (tool.contains("systems") && tool.at("systems").is_array()) {
        for (const Json& system : tool.at("systems")) {
            if (std::string name = stringAt(system, "archiveFileName"); !name.empty()) {
                names.insert(std::move(name));
            }
        }
    }
}

/// Archive names of a platform entry and of every system of its tools.
void addPlatformArchives(const Indexes& indexes, const Json& entry, std::set<std::string>& names) {
    names.insert(stringAt(entry, "archiveFileName"));
    for (const auto& tool : indexes.dependencies(entry)) {
        if (tool.entry != nullptr) {
            addToolArchives(*tool.entry, names);
        }
    }
}

BoardPackage parsePackage(const Json& json) {
    BoardPackage package;
    package.id = json.at("id").get<std::string>();
    package.nameFr = json.at("name").at("fr").get<std::string>();
    package.nameEn = json.at("name").at("en").get<std::string>();
    package.platform = json.at("platform").get<std::string>();
    package.version = stringAt(json, "version");
    package.indexUrl = stringAt(json, "indexUrl");
    package.shipped = json.value("shipped", false);
    if (json.contains("estimate")) {
        package.downloadBytes = json.at("estimate").value("download", std::uint64_t{0});
        package.diskBytes = json.at("estimate").value("disk", std::uint64_t{0});
    }
    return package;
}

} // namespace

Result<BoardPackages> BoardPackages::fromJson(std::string_view document, const BoardDatabase& boards) {
    const Json json = Json::parse(document, nullptr, /*allow_exceptions=*/false);
    if (!json.is_object() || !json.contains("packages") || !json.at("packages").is_array()) {
        return Error{ErrorCode::InvalidFormat, "board packages: a \"packages\" array is expected"};
    }
    BoardPackages result;
    std::set<std::string> ids;
    std::set<std::string> platforms;
    int shipped = 0;
    for (const Json& entry : json.at("packages")) {
        BoardPackage package;
        try {
            package = parsePackage(entry);
        } catch (const Json::exception& error) {
            return Error{ErrorCode::InvalidFormat, std::string("board packages: ") + error.what()};
        }
        if (package.id.empty() || !ids.insert(package.id).second) {
            return Error{ErrorCode::InvalidFormat, "board packages: id '" + package.id + "' is empty or used twice"};
        }
        if (package.platform.find(':') == std::string::npos || !platforms.insert(package.platform).second) {
            return Error{ErrorCode::InvalidFormat,
                         "board packages: platform '" + package.platform + "' is not vendor:arch or used twice"};
        }
        shipped += package.shipped ? 1 : 0;
        result.packages_.push_back(std::move(package));
    }
    if (shipped != 1) {
        return Error{ErrorCode::InvalidFormat, "board packages: exactly one package must be shipped"};
    }
    for (const Board& board : boards.boards()) {
        auto it = std::find_if(result.packages_.begin(), result.packages_.end(),
                               [&](const BoardPackage& package) { return package.platform == board.platform; });
        if (it == result.packages_.end()) {
            return Error{ErrorCode::InvalidFormat,
                         "board packages: no package provides board '" + board.id + "' (" + board.platform + ")"};
        }
        it->boards.push_back(board.id);
    }
    return result;
}

const BoardPackages& BoardPackages::builtin() {
    static const BoardPackages packages = [] {
        const auto document = resources::find("resources/board-packages.json");
        auto result = fromJson(document.value_or("{}"), BoardDatabase::builtin());
        // The embedded file is validated by the unit tests; never crash the application.
        return result.ok() ? std::move(result).value() : BoardPackages{};
    }();
    return packages;
}

const BoardPackage* BoardPackages::find(std::string_view id) const {
    const auto it = std::find_if(packages_.begin(), packages_.end(),
                                 [&](const BoardPackage& package) { return package.id == id; });
    return it == packages_.end() ? nullptr : &*it;
}

const BoardPackage* BoardPackages::forPlatform(std::string_view platform) const {
    const auto it = std::find_if(packages_.begin(), packages_.end(),
                                 [&](const BoardPackage& package) { return package.platform == platform; });
    return it == packages_.end() ? nullptr : &*it;
}

const BoardPackage* BoardPackages::forBoard(const Board& board) const {
    return forPlatform(board.platform);
}

std::string BoardPackages::additionalIndexUrls() const {
    std::string urls;
    for (const BoardPackage& package : packages_) {
        if (!package.indexUrl.empty()) {
            urls += (urls.empty() ? "" : " ") + package.indexUrl;
        }
    }
    return urls;
}

HostSystem currentHost() {
#if defined(_WIN32)
    return HostSystem::WindowsX64;
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return HostSystem::MacArm64;
#elif defined(__APPLE__)
    return HostSystem::MacX64;
#elif defined(__aarch64__)
    return HostSystem::LinuxArm64;
#else
    return HostSystem::LinuxX64;
#endif
}

std::string formatSize(std::uint64_t bytes, Language language) {
    const bool french = language == Language::French;
    const auto rounded = [&](std::uint64_t value, std::uint64_t unit) {
        const std::uint64_t tenths = (value * 10 + unit / 2) / unit;
        std::string text = std::to_string(tenths / 10);
        if (tenths < 100 && tenths % 10 != 0) {
            text += (french ? "," : ".") + std::to_string(tenths % 10);
        }
        return text;
    };
    constexpr std::uint64_t kMega = 1000ULL * 1000ULL;
    constexpr std::uint64_t kGiga = 1000ULL * kMega;
    if (bytes >= kGiga) {
        return rounded(bytes, kGiga) + (french ? " Go" : " GB");
    }
    if (bytes >= kMega) {
        return rounded(bytes, kMega) + (french ? " Mo" : " MB");
    }
    return std::to_string((bytes + 999) / 1000) + (french ? " ko" : " kB");
}

std::optional<std::uint64_t> downloadSize(const Directories& directories, std::string_view platform,
                                          std::string_view version, HostSystem host) {
    const Indexes indexes(directories.data);
    const Json* entry = indexes.platform(platform, version);
    if (entry == nullptr) {
        return std::nullopt;
    }
    std::error_code error;
    std::uint64_t total = 0;
    if (!fs::exists(archivePath(directories, stringAt(*entry, "archiveFileName")), error)) {
        total += numberAt(*entry, "size");
    }
    for (const auto& tool : indexes.dependencies(*entry)) {
        if (tool.entry == nullptr || fs::exists(toolDirectory(directories, tool), error)) {
            continue; // installed for another platform
        }
        if (const Json* system = systemFor(*tool.entry, host);
            system != nullptr && !fs::exists(archivePath(directories, stringAt(*system, "archiveFileName")), error)) {
            total += numberAt(*system, "size");
        }
    }
    return total;
}

std::uint64_t diskUsage(const Directories& directories, std::string_view platform, std::string_view version) {
    const auto [vendor, arch] = splitPlatform(platform);
    std::uint64_t total = directorySize(directories.data / "packages" / fs::u8path(vendor) / "hardware" /
                                        fs::u8path(arch) / fs::u8path(std::string(version)));
    const Indexes indexes(directories.data);
    if (const Json* entry = indexes.platform(platform, version); entry != nullptr) {
        total += directorySize(archivePath(directories, stringAt(*entry, "archiveFileName")));
        for (const auto& tool : indexes.dependencies(*entry)) {
            total += directorySize(toolDirectory(directories, tool));
            if (tool.entry != nullptr) {
                if (const Json* system = systemFor(*tool.entry, currentHost()); system != nullptr) {
                    total += directorySize(archivePath(directories, stringAt(*system, "archiveFileName")));
                }
            }
        }
    }
    return total;
}

std::vector<fs::path> archivesOnlyUsedBy(const Directories& directories, std::string_view platform,
                                         const std::vector<InstalledPlatform>& kept) {
    const Indexes indexes(directories.data);
    std::set<std::string> candidates;
    for (const Json* entry : indexes.platformVersions(platform)) {
        addPlatformArchives(indexes, *entry, candidates);
    }
    std::set<std::string> keep;
    for (const InstalledPlatform& other : kept) {
        if (other.id == platform) {
            continue;
        }
        if (const Json* entry = indexes.platform(other.id, other.version); entry != nullptr) {
            addPlatformArchives(indexes, *entry, keep);
        }
    }
    std::vector<fs::path> found;
    std::error_code error;
    for (const std::string& name : candidates) {
        if (!name.empty() && keep.count(name) == 0 && fs::is_regular_file(archivePath(directories, name), error)) {
            found.push_back(archivePath(directories, name));
        }
    }
    return found;
}

Result<std::vector<PackageState>> packageStates(const ArduinoCli& cli, const BoardPackages& packages) {
    auto installed = cli.installedPlatforms();
    if (!installed.ok()) {
        return installed.error();
    }
    std::vector<PackageState> states;
    for (const BoardPackage& package : packages.packages()) {
        PackageState state;
        state.package = &package;
        const auto it = std::find_if(installed.value().begin(), installed.value().end(),
                                     [&](const InstalledPlatform& platform) { return platform.id == package.platform; });
        if (it != installed.value().end()) {
            state.installedVersion = it->version;
            state.diskBytes = diskUsage(cli.directories(), package.platform, it->version);
        } else {
            state.diskBytes = package.diskBytes;
        }
        const bool wanted = !package.shipped && (state.installedVersion.empty() ||
                                                 (!package.version.empty() && state.installedVersion != package.version));
        if (wanted) {
            state.downloadBytes =
                downloadSize(cli.directories(), package.platform, package.version).value_or(package.downloadBytes);
        }
        states.push_back(state);
    }
    return states;
}

Result<CommandOutcome> installBoardPackage(const ArduinoCli& cli, const BoardPackage& package) {
    return cli.installPlatform(package.platform, package.version);
}

Result<CommandOutcome> installPackageOf(const ArduinoCli& cli, const Board& board, const BoardPackages& packages) {
    const BoardPackage* package = packages.forBoard(board);
    return package != nullptr ? installBoardPackage(cli, *package) : cli.installPlatform(board.platform);
}

Result<CommandOutcome> removeBoardPackage(const ArduinoCli& cli, const BoardPackage& package) {
    if (package.shipped) {
        return Error{ErrorCode::ProcessFailed, "the " + package.nameEn + " come with Scratch2Arduino"};
    }
    auto outcome = cli.uninstallPlatform(package.platform);
    if (!outcome.ok() || !outcome.value().success) {
        return outcome;
    }
    // The archives stay in the downloads folder after an uninstall, and they are a large part
    // of the space (1.9 GB for the ESP32).
    if (auto remaining = cli.installedPlatforms(); remaining.ok()) {
        std::error_code error;
        for (const fs::path& archive : archivesOnlyUsedBy(cli.directories(), package.platform, remaining.value())) {
            fs::remove(archive, error);
        }
    }
    return outcome;
}

} // namespace s2a::toolchain
