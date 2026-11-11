#include "s2a/userdata.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <utility>

namespace s2a::userdata {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr const char* kRecentFile = "recent.json";
constexpr const char* kSettingsFile = "settings.json";
constexpr const char* kAutosaveFile = "autosave.s2a";
constexpr const char* kAutosaveInfoFile = "autosave.json";
/// Bigger files are not ours: they are ignored rather than read into memory.
constexpr std::uintmax_t kMaxJsonSize = 1024 * 1024;
constexpr const char* kLibraryFile = "library.json";
/// A library holds many groups: larger limit than the other files.
constexpr std::uintmax_t kMaxLibrarySize = 16 * 1024 * 1024;
constexpr std::size_t kMaxNameLength = 80;

/// Name without surrounding spaces or line breaks, not too long, never empty.
std::string cleanName(std::string name) {
    for (char& c : name) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
    }
    const auto first = name.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "?";
    }
    name = name.substr(first, name.find_last_not_of(' ') - first + 1);
    if (name.size() > kMaxNameLength) {
        name.resize(kMaxNameLength);
        while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xC0U) == 0x80U) {
            name.pop_back(); // do not cut a UTF-8 character in two
        }
        if (!name.empty() && (static_cast<unsigned char>(name.back()) & 0x80U) != 0) {
            name.pop_back();
        }
    }
    return name;
}

fs::path environmentPath(const char* name) {
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): nothing calls setenv
    return value != nullptr && *value != '\0' ? fs::u8path(value) : fs::path{};
}

/// Content of a small text file, or nothing when it is missing, too big or unreadable.
std::optional<std::string> readSmallFile(const fs::path& path, std::uintmax_t maxSize = kMaxJsonSize) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || size > maxSize) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) {
        return std::nullopt;
    }
    return bytes;
}

std::optional<Json> readJson(const fs::path& path) {
    const auto bytes = readSmallFile(path);
    if (!bytes) {
        return std::nullopt;
    }
    Json json = Json::parse(*bytes, nullptr, /*allow_exceptions=*/false);
    if (json.is_discarded() || !json.is_object()) {
        return std::nullopt;
    }
    return json;
}

/// Absolute, normalized form used to compare paths.
fs::path normalized(const fs::path& path) {
    std::error_code error;
    fs::path absolute = fs::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

} // namespace

fs::path defaultDirectory() {
    if (fs::path custom = environmentPath("S2A_USER_DIR"); !custom.empty()) {
        return custom;
    }
#if defined(_WIN32)
    fs::path base = environmentPath("APPDATA");
    if (base.empty()) {
        base = environmentPath("USERPROFILE") / "AppData" / "Roaming";
    }
    return base / "Scratch2Arduino";
#elif defined(__APPLE__)
    return environmentPath("HOME") / "Library" / "Application Support" / "Scratch2Arduino";
#else
    fs::path base = environmentPath("XDG_CONFIG_HOME");
    if (base.empty()) {
        base = environmentPath("HOME") / ".config";
    }
    return base / "scratch2arduino";
#endif
}

Status writeFileAtomically(const fs::path& path, std::string_view bytes) {
    std::error_code error;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), error);
        if (error) {
            return Error{ErrorCode::WriteFailed, "cannot create " + path.parent_path().u8string() + ": " + error.message()};
        }
    }
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Error{ErrorCode::WriteFailed, "cannot create " + temporary.u8string()};
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            fs::remove(temporary, error);
            return Error{ErrorCode::WriteFailed, "cannot write " + temporary.u8string()};
        }
    }
    fs::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return Error{ErrorCode::WriteFailed, "cannot replace " + path.u8string() + ": " + error.message()};
    }
    return success();
}

Store::Store(fs::path directory) : directory_(std::move(directory)) {}

// --- Recent projects -------------------------------------------------------------------

std::vector<fs::path> Store::readRecent() const {
    std::vector<fs::path> paths;
    const auto json = readJson(directory_ / kRecentFile);
    if (!json || !json->contains("recent") || !(*json)["recent"].is_array()) {
        return paths;
    }
    for (const Json& entry : (*json)["recent"]) {
        if (entry.is_string() && !entry.get<std::string>().empty() && paths.size() < kMaxRecent) {
            paths.push_back(fs::u8path(entry.get<std::string>()));
        }
    }
    return paths;
}

std::vector<fs::path> Store::recentProjects() const {
    std::vector<fs::path> paths = readRecent();
    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [](const fs::path& path) {
                                   std::error_code error;
                                   return !fs::is_regular_file(path, error);
                               }),
                paths.end());
    return paths;
}

Status Store::addRecentProject(const fs::path& project) {
    const fs::path added = normalized(project);
    std::vector<fs::path> paths = readRecent();
    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [&added](const fs::path& path) { return normalized(path) == added; }),
                paths.end());
    paths.insert(paths.begin(), added);
    if (paths.size() > kMaxRecent) {
        paths.resize(kMaxRecent);
    }
    Json list = Json::array();
    for (const fs::path& path : paths) {
        list.push_back(path.u8string());
    }
    return writeFileAtomically(directory_ / kRecentFile, Json{{"recent", list}}.dump(2) + "\n");
}

bool Store::isRecentProject(const fs::path& project) const {
    const fs::path wanted = normalized(project);
    const std::vector<fs::path> paths = readRecent();
    return std::any_of(paths.begin(), paths.end(), [&wanted](const fs::path& path) { return normalized(path) == wanted; });
}

// --- Autosave --------------------------------------------------------------------------

Status Store::autosave(const project::Project& project, const fs::path& originalPath) {
    std::error_code error;
    fs::create_directories(directory_, error);
    if (error) {
        return Error{ErrorCode::WriteFailed, "cannot create " + directory_.u8string() + ": " + error.message()};
    }
    if (auto saved = project::save(project, directory_ / kAutosaveFile); !saved.ok()) {
        return saved;
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
    const Json info{{"originalPath", originalPath.empty() ? std::string{} : normalized(originalPath).u8string()},
                    {"savedAt", static_cast<std::int64_t>(now.count())}};
    return writeFileAtomically(directory_ / kAutosaveInfoFile, info.dump(2) + "\n");
}

std::optional<Store::Autosaved> Store::autosaved() const {
    std::error_code error;
    if (!fs::is_regular_file(directory_ / kAutosaveFile, error)) {
        return std::nullopt;
    }
    auto loaded = project::load(directory_ / kAutosaveFile);
    if (!loaded.ok()) {
        return std::nullopt;
    }
    Autosaved result{std::move(loaded).value(), {}, 0};
    if (const auto info = readJson(directory_ / kAutosaveInfoFile)) {
        if (const auto path = info->find("originalPath"); path != info->end() && path->is_string()) {
            result.originalPath = fs::u8path(path->get<std::string>());
        }
        if (const auto at = info->find("savedAt"); at != info->end() && at->is_number_integer()) {
            result.savedAt = at->get<std::int64_t>();
        }
    }
    return result;
}

void Store::clearAutosave() const {
    std::error_code ignored;
    fs::remove(directory_ / kAutosaveFile, ignored);
    fs::remove(directory_ / kAutosaveInfoFile, ignored);
}

// --- "My blocks" -----------------------------------------------------------------------

std::string libraryToJson(const std::vector<SavedBlocks>& groups) {
    Json items = Json::array();
    for (const SavedBlocks& group : groups) {
        items.push_back(
            {{"id", group.id}, {"name", group.name}, {"blocks", Json::parse(project::toJson(group.blocks))}});
    }
    const Json document{{"format", std::string(kLibraryFormatName)},
                        {"formatVersion", kLibraryFormatVersion},
                        {"groups", items}};
    return document.dump(2) + "\n";
}

Result<std::vector<SavedBlocks>> libraryFromJson(std::string_view json) {
    const Json document = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded()) {
        return Error{ErrorCode::InvalidJson, "the blocks file is not valid JSON"};
    }
    if (!document.is_object() || document.value("format", std::string{}) != kLibraryFormatName ||
        !document.contains("groups") || !document["groups"].is_array()) {
        return Error{ErrorCode::InvalidFormat, "not a Scratch2Arduino blocks file"};
    }
    const auto version = document.value("formatVersion", 0);
    if (version < 1 || version > kLibraryFormatVersion) {
        return Error{ErrorCode::UnsupportedVersion, "blocks file version " + std::to_string(version)};
    }
    std::vector<SavedBlocks> groups;
    for (const Json& item : document["groups"]) {
        if (!item.is_object() || !item.contains("blocks")) {
            continue;
        }
        auto blocks = project::fromJson(item["blocks"].dump());
        if (!blocks.ok()) {
            continue; // one damaged group does not hide the others
        }
        const auto id = item.value("id", std::string{});
        groups.push_back({id.empty() ? project::newId() : id, cleanName(item.value("name", std::string{})),
                          std::move(blocks).value()});
    }
    return groups;
}

std::vector<SavedBlocks> Store::library() const {
    const auto bytes = readSmallFile(directory_ / kLibraryFile, kMaxLibrarySize);
    if (!bytes) {
        return {};
    }
    auto groups = libraryFromJson(*bytes);
    return groups.ok() ? std::move(groups).value() : std::vector<SavedBlocks>{};
}

Status Store::writeLibrary(const std::vector<SavedBlocks>& groups) const {
    return writeFileAtomically(directory_ / kLibraryFile, libraryToJson(groups));
}

Result<SavedBlocks> Store::addToLibrary(std::string name, project::Project blocks) {
    std::vector<SavedBlocks> groups = library();
    blocks.assets.clear(); // a group is blocks only
    SavedBlocks group{project::newId(), cleanName(std::move(name)), std::move(blocks)};
    groups.push_back(group);
    if (auto written = writeLibrary(groups); !written.ok()) {
        return written.error();
    }
    return group;
}

Status Store::renameInLibrary(std::string_view id, std::string name) {
    std::vector<SavedBlocks> groups = library();
    const auto found = std::find_if(groups.begin(), groups.end(), [id](const SavedBlocks& group) { return group.id == id; });
    if (found == groups.end()) {
        return Error{ErrorCode::FileNotFound, "no group " + std::string(id) + " in the library"};
    }
    found->name = cleanName(std::move(name));
    return writeLibrary(groups);
}

Status Store::removeFromLibrary(std::string_view id) {
    std::vector<SavedBlocks> groups = library();
    const auto found = std::find_if(groups.begin(), groups.end(), [id](const SavedBlocks& group) { return group.id == id; });
    if (found == groups.end()) {
        return Error{ErrorCode::FileNotFound, "no group " + std::string(id) + " in the library"};
    }
    groups.erase(found);
    return writeLibrary(groups);
}

Status Store::exportLibrary(const fs::path& file) const {
    return writeFileAtomically(file, libraryToJson(library()));
}

Result<std::size_t> Store::importLibrary(const fs::path& file) {
    std::error_code error;
    if (!fs::is_regular_file(file, error)) {
        return Error{ErrorCode::FileNotFound, file.u8string() + " does not exist"};
    }
    const auto bytes = readSmallFile(file, kMaxLibrarySize);
    if (!bytes) {
        return Error{ErrorCode::ReadFailed, "cannot read " + file.u8string()};
    }
    auto imported = libraryFromJson(*bytes);
    if (!imported.ok()) {
        return imported.error();
    }
    std::vector<SavedBlocks> groups = library();
    const auto nameUsed = [&groups](const std::string& name) {
        return std::any_of(groups.begin(), groups.end(), [&name](const SavedBlocks& group) { return group.name == name; });
    };
    for (SavedBlocks& group : imported.value()) {
        group.id = project::newId();
        if (nameUsed(group.name)) {
            int suffix = 2;
            while (nameUsed(group.name + " (" + std::to_string(suffix) + ")")) {
                ++suffix;
            }
            group.name += " (" + std::to_string(suffix) + ")";
        }
        groups.push_back(std::move(group));
    }
    if (auto written = writeLibrary(groups); !written.ok()) {
        return written.error();
    }
    return imported.value().size();
}

namespace {

bool knownLanguage(const std::string& code) {
    return code == "fr" || code == "en";
}

} // namespace

Settings Store::settings() const {
    Settings settings;
    const auto json = readJson(directory_ / kSettingsFile);
    if (!json || !json->is_object()) {
        return settings;
    }
    if (const auto language = json->find("language"); language != json->end() && language->is_string() &&
                                                      knownLanguage(language->get<std::string>())) {
        settings.language = language->get<std::string>();
    }
    if (const auto check = json->find("checkUpdates"); check != json->end() && check->is_boolean()) {
        settings.checkUpdates = check->get<bool>();
    }
    return settings;
}

Status Store::saveSettings(const Settings& settings) const {
    const Json json{{"language", knownLanguage(settings.language) ? settings.language : std::string{}},
                    {"checkUpdates", settings.checkUpdates}};
    return writeFileAtomically(directory_ / kSettingsFile, json.dump(2) + "\n");
}

} // namespace s2a::userdata
