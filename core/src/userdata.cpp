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
constexpr const char* kAutosaveFile = "autosave.s2a";
constexpr const char* kAutosaveInfoFile = "autosave.json";
/// Bigger files are not ours: they are ignored rather than read into memory.
constexpr std::uintmax_t kMaxJsonSize = 1024 * 1024;

fs::path environmentPath(const char* name) {
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): nothing calls setenv
    return value != nullptr && *value != '\0' ? fs::u8path(value) : fs::path{};
}

/// Content of a small text file, or nothing when it is missing, too big or unreadable.
std::optional<std::string> readSmallFile(const fs::path& path) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || size > kMaxJsonSize) {
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

} // namespace s2a::userdata
