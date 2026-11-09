#include "s2a/userdata.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace {

namespace fs = std::filesystem;
using s2a::userdata::Store;

class TempDir {
public:
    TempDir() : path_(fs::temp_directory_path() / ("s2a-userdata-" + s2a::project::newId())) {}
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "x";
}

void write(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

TEST(UserData, EmptyFolderHasNothing) {
    const TempDir dir;
    const Store store(dir.path() / "data"); // does not exist yet
    EXPECT_TRUE(store.recentProjects().empty());
    EXPECT_FALSE(store.autosaved().has_value());
    EXPECT_FALSE(store.isRecentProject(dir.path() / "a.s2a"));
    store.clearAutosave(); // nothing to remove: no error
}

TEST(UserData, RecentProjectsNewestFirstWithoutDuplicates) {
    const TempDir dir;
    Store store(dir.path() / "data");
    const fs::path a = dir.path() / "a.s2a";
    const fs::path b = dir.path() / "b été.s2a"; // non-ASCII on purpose
    touch(a);
    touch(b);
    ASSERT_TRUE(store.addRecentProject(a).ok());
    ASSERT_TRUE(store.addRecentProject(b).ok());
    ASSERT_TRUE(store.addRecentProject(dir.path() / "." / "a.s2a").ok()); // same file, other spelling

    const auto recent = store.recentProjects();
    ASSERT_EQ(recent.size(), 2U);
    EXPECT_EQ(recent[0], a.lexically_normal());
    EXPECT_EQ(recent[1], b.lexically_normal());
    EXPECT_TRUE(store.isRecentProject(b));
    EXPECT_FALSE(store.isRecentProject(dir.path() / "other.s2a"));

    // Read again by another store (next start of the program).
    EXPECT_EQ(Store(dir.path() / "data").recentProjects(), recent);
}

TEST(UserData, RecentProjectsAreCappedAndSkipMissingFiles) {
    const TempDir dir;
    Store store(dir.path());
    for (std::size_t i = 0; i < Store::kMaxRecent + 3; ++i) {
        const fs::path file = dir.path() / ("p" + std::to_string(i) + ".s2a");
        touch(file);
        ASSERT_TRUE(store.addRecentProject(file).ok());
    }
    auto recent = store.recentProjects();
    ASSERT_EQ(recent.size(), Store::kMaxRecent);
    EXPECT_EQ(recent.front().filename(), fs::path("p12.s2a"));

    fs::remove(dir.path() / "p12.s2a");
    recent = store.recentProjects();
    EXPECT_EQ(recent.size(), Store::kMaxRecent - 1);
    EXPECT_EQ(recent.front().filename(), fs::path("p11.s2a"));
}

TEST(UserData, DamagedFilesAreIgnored) {
    const TempDir dir;
    write(dir.path() / "recent.json", "{ not json");
    write(dir.path() / "autosave.s2a", "garbage");
    Store store(dir.path());
    EXPECT_TRUE(store.recentProjects().empty());
    EXPECT_FALSE(store.autosaved().has_value());

    write(dir.path() / "recent.json", R"({"recent": [42, "", null]})");
    EXPECT_TRUE(store.recentProjects().empty());

    // A damaged list is replaced by the next addition.
    const fs::path a = dir.path() / "a.s2a";
    touch(a);
    ASSERT_TRUE(store.addRecentProject(a).ok());
    EXPECT_EQ(store.recentProjects().size(), 1U);
}

TEST(UserData, AutosaveRoundTripAndClear) {
    const TempDir dir;
    Store store(dir.path() / "data");
    s2a::project::Project project = s2a::project::newProject(s2a::Language::French);
    project.name = "Mon robot";
    project.variables.push_back({"v1", "vitesse", 2});

    ASSERT_TRUE(store.autosave(project, dir.path() / "robot.s2a").ok());
    auto restored = store.autosaved();
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(s2a::project::toJson(restored->project), s2a::project::toJson(project));
    EXPECT_EQ(restored->originalPath, (dir.path() / "robot.s2a").lexically_normal());
    EXPECT_GT(restored->savedAt, 0);

    // A never saved project has no original path.
    ASSERT_TRUE(store.autosave(project, {}).ok());
    restored = store.autosaved();
    ASSERT_TRUE(restored.has_value());
    EXPECT_TRUE(restored->originalPath.empty());

    store.clearAutosave();
    EXPECT_FALSE(store.autosaved().has_value());
}

TEST(UserData, WriteFileAtomicallyReplacesContent) {
    const TempDir dir;
    const fs::path file = dir.path() / "sub" / "file.txt";
    ASSERT_TRUE(s2a::userdata::writeFileAtomically(file, "one").ok());
    ASSERT_TRUE(s2a::userdata::writeFileAtomically(file, "two").ok());
    std::ifstream in(file);
    std::string content;
    in >> content;
    EXPECT_EQ(content, "two");
    EXPECT_FALSE(fs::exists(dir.path() / "sub" / "file.txt.tmp"));
}

TEST(UserData, DefaultDirectoryIsNamedAfterTheProgram) {
    const fs::path dir = s2a::userdata::defaultDirectory();
    EXPECT_FALSE(dir.empty());
}

} // namespace
