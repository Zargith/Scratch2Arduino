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

/// A group: "set score to 0" then "call flash" (uses a variable and a function).
s2a::project::Project group() {
    s2a::project::Project blocks;
    blocks.name = "groupe";
    blocks.variables.push_back({"v1", "score", 0});
    blocks.functions.push_back({"f1", "flash"});
    s2a::project::Block set;
    set.id = "b1";
    set.type = "variables_set";
    set.fields["VARIABLE"] = "v1";
    set.inputs["VALUE"].value = "0";
    s2a::project::Block call;
    call.id = "b2";
    call.type = "functions_call";
    call.fields["FUNCTION"] = "f1";
    blocks.scripts.push_back({0, 0, {set, call}});
    return blocks;
}

TEST(UserData, LibraryAddRenameRemove) {
    const TempDir dir;
    Store store(dir.path() / "data");
    EXPECT_TRUE(store.library().empty());

    auto first = store.addToLibrary("  Remise à zéro\n", group());
    ASSERT_TRUE(first.ok());
    EXPECT_EQ(first.value().name, "Remise à zéro");
    auto second = store.addToLibrary("", group());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(second.value().name, "?");
    EXPECT_NE(first.value().id, second.value().id);

    auto groups = Store(dir.path() / "data").library();
    ASSERT_EQ(groups.size(), 2U);
    EXPECT_EQ(groups[0].name, "Remise à zéro");
    EXPECT_EQ(s2a::project::toJson(groups[0].blocks), s2a::project::toJson(group()));

    ASSERT_TRUE(store.renameInLibrary(second.value().id, "Clignoter").ok());
    EXPECT_EQ(store.library()[1].name, "Clignoter");
    ASSERT_TRUE(store.removeFromLibrary(first.value().id).ok());
    groups = store.library();
    ASSERT_EQ(groups.size(), 1U);
    EXPECT_EQ(groups[0].name, "Clignoter");

    EXPECT_EQ(store.renameInLibrary("missing", "x").error().code, s2a::ErrorCode::FileNotFound);
    EXPECT_EQ(store.removeFromLibrary("missing").error().code, s2a::ErrorCode::FileNotFound);
}

TEST(UserData, LibraryNamesAreShortened) {
    const TempDir dir;
    Store store(dir.path());
    std::string longName;
    for (int i = 0; i < 100; ++i) {
        longName += "é"; // 2 bytes each: the cut must not split one
    }
    auto added = store.addToLibrary(longName, group());
    ASSERT_TRUE(added.ok());
    EXPECT_LE(added.value().name.size(), 80U);
    EXPECT_EQ(added.value().name.size() % 2, 0U);
}

TEST(UserData, LibrarySurvivesExportAndImport) {
    const TempDir dir;
    Store home(dir.path() / "home");
    ASSERT_TRUE(home.addToLibrary("Remise à zéro", group()).ok());
    ASSERT_TRUE(home.addToLibrary("Clignoter", group()).ok());
    const fs::path file = dir.path() / "mes blocs.s2ablocks";
    ASSERT_TRUE(home.exportLibrary(file).ok());

    // Another computer, which already has a group with the same name.
    Store school(dir.path() / "school");
    ASSERT_TRUE(school.addToLibrary("Clignoter", group()).ok());
    auto imported = school.importLibrary(file);
    ASSERT_TRUE(imported.ok());
    EXPECT_EQ(imported.value(), 2U);
    const auto groups = school.library();
    ASSERT_EQ(groups.size(), 3U);
    EXPECT_EQ(groups[1].name, "Remise à zéro");
    EXPECT_EQ(groups[2].name, "Clignoter (2)");
    EXPECT_EQ(s2a::project::toJson(groups[1].blocks), s2a::project::toJson(group()));
    EXPECT_NE(groups[1].id, home.library()[0].id); // new ids

    // Importing twice keeps adding, with other names.
    ASSERT_TRUE(school.importLibrary(file).ok());
    EXPECT_EQ(school.library().back().name, "Clignoter (3)");
}

TEST(UserData, LibraryImportRejectsOtherFiles) {
    const TempDir dir;
    Store store(dir.path());
    write(dir.path() / "bad.s2ablocks", "{ nope");
    EXPECT_EQ(store.importLibrary(dir.path() / "bad.s2ablocks").error().code, s2a::ErrorCode::InvalidJson);
    write(dir.path() / "project.s2ablocks", R"({"format": "scratch2arduino-project", "groups": []})");
    EXPECT_EQ(store.importLibrary(dir.path() / "project.s2ablocks").error().code, s2a::ErrorCode::InvalidFormat);
    write(dir.path() / "future.s2ablocks", R"({"format": "scratch2arduino-blocks", "formatVersion": 99, "groups": []})");
    EXPECT_EQ(store.importLibrary(dir.path() / "future.s2ablocks").error().code, s2a::ErrorCode::UnsupportedVersion);
    EXPECT_EQ(store.importLibrary(dir.path() / "missing.s2ablocks").error().code, s2a::ErrorCode::FileNotFound);

    // A damaged group is skipped, the others are kept.
    const std::string valid = s2a::userdata::libraryToJson({{"g1", "ok", group()}});
    std::string mixed = valid;
    mixed.replace(mixed.find("\"groups\": ["), 11, R"("groups": [{"name": "broken", "blocks": 3},)");
    write(dir.path() / "mixed.s2ablocks", mixed);
    auto imported = store.importLibrary(dir.path() / "mixed.s2ablocks");
    ASSERT_TRUE(imported.ok());
    EXPECT_EQ(imported.value(), 1U);
    EXPECT_TRUE(store.library().size() == 1U && store.library()[0].name == "ok");
}

TEST(UserData, DefaultDirectoryIsNamedAfterTheProgram) {
    const fs::path dir = s2a::userdata::defaultDirectory();
    EXPECT_FALSE(dir.empty());
}

} // namespace
