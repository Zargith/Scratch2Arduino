#pragma once

#include "s2a/project.hpp"
#include "s2a/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace s2a::userdata {

/// Folder of the user's own data (recent projects, autosave...):
/// %APPDATA%\Scratch2Arduino on Windows, ~/Library/Application Support/Scratch2Arduino on
/// macOS, $XDG_CONFIG_HOME/scratch2arduino (~/.config/scratch2arduino) elsewhere.
/// The environment variable S2A_USER_DIR replaces it (tests, portable installations).
/// Not created here: the Store creates it on the first write.
[[nodiscard]] std::filesystem::path defaultDirectory();

/// Writes `bytes` to `path` through a temporary file, so the old content is never left
/// half-written. Creates the parent folders.
[[nodiscard]] Status writeFileAtomically(const std::filesystem::path& path, std::string_view bytes);

/// The user's data in one folder. Every reader tolerates missing or damaged files (it
/// behaves as if they were empty): this data is a convenience, never a reason to fail.
class Store {
public:
    /// Recent projects kept in the list.
    static constexpr std::size_t kMaxRecent = 10;

    explicit Store(std::filesystem::path directory);

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }

    // --- Recent projects -------------------------------------------------------------

    /// Projects opened or saved lately, newest first. Files that no longer exist are left out.
    [[nodiscard]] std::vector<std::filesystem::path> recentProjects() const;

    /// Puts `project` (made absolute) at the top of the list, without duplicates.
    [[nodiscard]] Status addRecentProject(const std::filesystem::path& project);

    /// True when `project` is in the list: the editor may only reopen those by path.
    [[nodiscard]] bool isRecentProject(const std::filesystem::path& project) const;

    // --- Autosave ------------------------------------------------------------------------

    struct Autosaved {
        project::Project project;
        /// Where the project was last saved by the user; empty for a never saved project.
        std::filesystem::path originalPath;
        /// When the autosave was written (seconds since 1970-01-01 UTC).
        std::int64_t savedAt = 0;
    };

    /// Keeps a copy of the project being edited, restored after a crash or a close without saving.
    [[nodiscard]] Status autosave(const project::Project& project, const std::filesystem::path& originalPath);

    /// The last autosave, if one is there and readable.
    [[nodiscard]] std::optional<Autosaved> autosaved() const;

    /// Forgets the autosave (the project was saved, or the user dropped it).
    void clearAutosave() const;

private:
    [[nodiscard]] std::vector<std::filesystem::path> readRecent() const;

    std::filesystem::path directory_;
};

} // namespace s2a::userdata
