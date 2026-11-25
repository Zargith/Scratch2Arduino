#include "s2a/project.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <miniz.h>
#include <optional>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace s2a::project;
using s2a::ErrorCode;

Block block(std::string id, std::string type) {
    Block result;
    result.id = std::move(id);
    result.type = std::move(type);
    return result;
}

/// Blink: when the program starts, forever { LED on, wait 0.5 + 0.5 s, LED off, wait 1 s }.
Project blink() {
    Project project;
    project.name = "Clignotant été"; // non-ASCII on purpose
    project.boardId = "uno";
    project.variables.push_back({"v1", "compteur", 3.5});

    Block on = block("b3", "pins_builtin_led");
    on.fields["STATE"] = "ON";
    Block sum = block("b5", "operators_add");
    sum.inputs["NUM1"].value = "0.5";
    sum.inputs["NUM2"].value = "0.5";
    Block waitSum = block("b4", "control_wait");
    waitSum.inputs["SECONDS"].value = "1";
    waitSum.inputs["SECONDS"].block = sum;
    Block off = block("b6", "pins_builtin_led");
    off.fields["STATE"] = "OFF";
    Block wait = block("b7", "control_wait");
    wait.inputs["SECONDS"].value = "1";

    Block forever = block("b2", "control_forever");
    forever.statements["SUBSTACK"] = {on, waitSum, off, wait};

    Script script;
    script.x = 40;
    script.y = 60.25;
    script.blocks = {block("b1", "events_when_program_starts"), forever};
    project.scripts.push_back(script);
    return project;
}

/// A unique, empty directory removed at the end of the test.
class TempDir {
public:
    TempDir() : path_(fs::temp_directory_path() / ("s2a-test-" + newId())) { fs::create_directories(path_); }
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

std::string zipOf(const std::map<std::string, std::string>& entries) {
    mz_zip_archive zip{};
    EXPECT_TRUE(mz_zip_writer_init_heap(&zip, 0, 0));
    for (const auto& [name, content] : entries) {
        EXPECT_TRUE(mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(), MZ_DEFAULT_LEVEL));
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    EXPECT_TRUE(mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size));
    std::string bytes(static_cast<const char*>(buffer), size);
    mz_free(buffer);
    mz_zip_writer_end(&zip);
    return bytes;
}

std::string zipWith(const std::string& name, const std::string& content) {
    return zipOf({{name, content}});
}

/// Content of one entry of a zip archive, nullopt when it has no such entry.
std::optional<std::string> entryOf(const std::string& bytes, const std::string& name) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        return std::nullopt;
    }
    std::optional<std::string> content;
    std::size_t size = 0;
    if (void* data = mz_zip_reader_extract_file_to_heap(&zip, name.c_str(), &size, 0)) {
        content = std::string(static_cast<const char*>(data), size);
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return content;
}

// --- Model -------------------------------------------------------------------------------

TEST(NewProject, HasAStartBlockAndTheDefaultBoard) {
    const Project french = newProject(s2a::Language::French);
    EXPECT_EQ(french.name, "Mon projet");
    EXPECT_EQ(french.boardId, "uno");
    ASSERT_EQ(french.scripts.size(), 1U);
    ASSERT_EQ(french.scripts[0].blocks.size(), 1U);
    EXPECT_EQ(french.scripts[0].blocks[0].type, "events_when_program_starts");
    EXPECT_EQ(newProject(s2a::Language::English).name, "My project");
}

TEST(NewId, IsRandomAndAlphanumeric) {
    const std::string a = newId();
    const std::string b = newId();
    EXPECT_EQ(a.size(), 20U);
    EXPECT_NE(a, b);
    EXPECT_EQ(a.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"),
              std::string::npos);
}

TEST(Box, CopiesDeeply) {
    Input original;
    original.block = block("x", "operators_add");
    Input copy = original;
    copy.block->type = "operators_subtract";
    EXPECT_EQ(original.block->type, "operators_add");
    copy.block.reset();
    EXPECT_FALSE(copy.block);
    EXPECT_TRUE(original.block);
}

TEST(ForEachBlock, VisitsNestedBlocksDepthFirst) {
    std::vector<std::string> ids;
    forEachBlock(blink(), [&](const Block& visited) { ids.push_back(visited.id); });
    EXPECT_EQ(ids, (std::vector<std::string>{"b1", "b2", "b3", "b4", "b5", "b6", "b7"}));
}

// --- project.json --------------------------------------------------------------------------

// Audit 2026-11-24: a project nested very deep overflowed the stack instead of being refused.
TEST(ProjectJson, RefusesJsonNestedTooDeep) {
    const std::size_t depth = 200000;
    const auto parsed = fromJson(R"({"format": "scratch2arduino-project", "formatVersion": 1, "x": )" +
                                 std::string(depth, '[') + std::string(depth, ']') + "}");
    ASSERT_FALSE(parsed.ok());
    EXPECT_EQ(parsed.error().code, s2a::ErrorCode::InvalidJson);
}

TEST(ProjectJson, RoundTripsExactly) {
    const std::string json = toJson(blink());
    const auto parsed = fromJson(json);
    ASSERT_TRUE(parsed.ok()) << parsed.error().detail;
    EXPECT_EQ(toJson(parsed.value()), json);
    EXPECT_EQ(parsed.value().name, "Clignotant été");
    EXPECT_EQ(parsed.value().variables.at(0).initialValue, 3.5);
    const Block& forever = parsed.value().scripts.at(0).blocks.at(1);
    EXPECT_EQ(forever.statements.at("SUBSTACK").at(1).inputs.at("SECONDS").block->type, "operators_add");
}

TEST(ProjectJson, RoundTripsFunctionsAndLeavesThemOutWhenThereAreNone) {
    Project project = blink();
    EXPECT_EQ(toJson(project).find("\"functions\""), std::string::npos) << "older projects keep their bytes";
    project.functions.push_back({"f1", "clignoter vite", {}});
    const std::string json = toJson(project);
    const auto parsed = fromJson(json);
    ASSERT_TRUE(parsed.ok()) << parsed.error().detail;
    ASSERT_EQ(parsed.value().functions.size(), 1U);
    EXPECT_EQ(parsed.value().functions[0].name, "clignoter vite");
    EXPECT_EQ(toJson(parsed.value()), json);
}

TEST(ProjectJson, RoundTripsFunctionParametersAndLeavesThemOutWhenThereAreNone) {
    Project project = blink();
    project.functions.push_back({"f1", "biper", {"nombre", "durée"}});
    project.functions.push_back({"f2", "attendre", {}});
    const std::string json = toJson(project);
    EXPECT_NE(json.find("\"parameters\": [\n        \"nombre\""), std::string::npos) << json;
    EXPECT_EQ(json.find("\"parameters\"", json.find("\"attendre\"")), std::string::npos) << "no inputs: left out";
    const auto parsed = fromJson(json);
    ASSERT_TRUE(parsed.ok()) << parsed.error().detail;
    EXPECT_EQ(parsed.value().functions.at(0).parameters, (std::vector<std::string>{"nombre", "durée"}));
    EXPECT_TRUE(parsed.value().functions.at(1).parameters.empty());
    EXPECT_EQ(toJson(parsed.value()), json);
}

TEST(ProjectJson, RefusesAParameterThatIsNotAName) {
    Project project = blink();
    project.functions.push_back({"f1", "biper", {"nombre"}});
    std::string json = toJson(project);
    json.replace(json.find("\"nombre\""), 8, "12");
    const auto parsed = fromJson(json);
    ASSERT_FALSE(parsed.ok());
    EXPECT_NE(parsed.error().detail.find("$.functions[0].parameters[0]"), std::string::npos) << parsed.error().detail;
}

TEST(ProjectJson, RoundTripsTheLibrariesAndLeavesThemOutWhenThereAreNone) {
    Project project = blink();
    EXPECT_EQ(toJson(project).find("\"libraries\""), std::string::npos) << "older projects keep their bytes";
    project.libraries = {{"ArduinoJson", "7.2.0"}, {"Bounce2", ""}};
    const std::string json = toJson(project);
    EXPECT_EQ(json.find("\"version\": \"\""), std::string::npos) << "no version: left out";
    const auto parsed = fromJson(json);
    ASSERT_TRUE(parsed.ok()) << parsed.error().detail;
    ASSERT_EQ(parsed.value().libraries.size(), 2U);
    EXPECT_EQ(parsed.value().libraries[0].name, "ArduinoJson");
    EXPECT_EQ(parsed.value().libraries[0].version, "7.2.0");
    EXPECT_EQ(parsed.value().libraries[1].version, "");
    EXPECT_EQ(toJson(parsed.value()), json);
    EXPECT_FALSE(fromJson(R"({"format": "scratch2arduino-project", "formatVersion": 1, "board": "uno",
        "variables": [], "scripts": [], "libraries": [3]})")
                     .ok());
}

TEST(ProjectJson, IsReadableAndVersioned) {
    const std::string json = toJson(blink());
    EXPECT_NE(json.find("\"format\": \"scratch2arduino-project\""), std::string::npos);
    EXPECT_NE(json.find("\"formatVersion\": 1"), std::string::npos);
    EXPECT_EQ(json.back(), '\n');
}

TEST(ProjectJson, BlockProjectsStayInVersion1) {
    const std::string json = toJson(blink());
    EXPECT_EQ(json.find("\"language\""), std::string::npos) << "older versions open block projects";
    EXPECT_EQ(json.find("\"code\""), std::string::npos);
    EXPECT_EQ(fromJson(json).value().language, ProgramLanguage::Blocks);
}

TEST(ProjectJson, RoundTripsTextProgramsInVersion2) {
    for (const ProgramLanguage language : {ProgramLanguage::Cpp, ProgramLanguage::Python, ProgramLanguage::JavaScript}) {
        Project project = blink(); // the blocks are kept beside the text
        project.language = language;
        project.code = "void setup() {\n\tpinMode(13, OUTPUT); // \"été\"\n}\n";
        const std::string json = toJson(project);
        EXPECT_NE(json.find("\"formatVersion\": 2"), std::string::npos);
        EXPECT_NE(json.find("\"language\": \"" + std::string(programLanguageId(language)) + "\""), std::string::npos);
        const auto parsed = fromJson(json);
        ASSERT_TRUE(parsed.ok()) << parsed.error().detail;
        EXPECT_EQ(parsed.value().language, language);
        EXPECT_EQ(parsed.value().code, project.code);
        EXPECT_EQ(parsed.value().scripts.size(), 1U);
        EXPECT_EQ(toJson(parsed.value()), json);
    }
}

TEST(ProjectJson, BlockProjectsDropTheCode) {
    Project project = blink();
    project.code = "leftover";
    EXPECT_EQ(toJson(project).find("leftover"), std::string::npos);
}

TEST(ProjectJson, MigratesVersion1ToBlocks) {
    // A version 1 file cannot hold a text program, even if a tool added the keys.
    const auto parsed = fromJson(R"json({"format": "scratch2arduino-project", "formatVersion": 1, "board": "uno",
        "language": "python", "code": "print(1)"})json");
    ASSERT_TRUE(parsed.ok()) << parsed.error().detail;
    EXPECT_EQ(parsed.value().language, ProgramLanguage::Blocks);
    EXPECT_EQ(parsed.value().code, "");
}

TEST(ProjectJson, RejectsUnknownLanguages) {
    const auto parsed = fromJson(R"({"format": "scratch2arduino-project", "formatVersion": 2, "board": "uno",
        "language": "cobol", "code": ""})");
    ASSERT_FALSE(parsed.ok());
    EXPECT_EQ(parsed.error().code, ErrorCode::InvalidFormat);
    EXPECT_EQ(programLanguageFromId("cpp"), ProgramLanguage::Cpp);
    EXPECT_FALSE(programLanguageFromId("C++"));
}

TEST(ProjectJson, KeepsTheUserCodeBesideTheBlocks) {
    Project project = blink();
    project.userCode = UserCode{ProgramLanguage::Python, "def clignoter(fois):\n    pass\n"};
    const std::string json = toJson(project);
    EXPECT_NE(json.find("\"formatVersion\": 2"), std::string::npos) << "older versions would lose the code";
    const auto parsed = fromJson(json);
    ASSERT_TRUE(parsed.ok()) << parsed.error().detail;
    EXPECT_EQ(parsed.value().language, ProgramLanguage::Blocks);
    ASSERT_TRUE(parsed.value().userCode.has_value());
    EXPECT_EQ(parsed.value().userCode->language, ProgramLanguage::Python);
    EXPECT_EQ(parsed.value().userCode->code, project.userCode->code);
    EXPECT_EQ(toJson(parsed.value()), json);
    const auto bytes = toArchive(project);
    ASSERT_TRUE(bytes.ok());
    EXPECT_EQ(fromArchive(bytes.value()).value().userCode->code, project.userCode->code);
}

TEST(ProjectJson, RejectsUserCodeInBlocks) {
    const auto parsed = fromJson(R"json({"format": "scratch2arduino-project", "formatVersion": 2, "board": "uno",
        "userCode": {"language": "blocks", "code": ""}})json");
    ASSERT_FALSE(parsed.ok());
    EXPECT_EQ(parsed.error().code, ErrorCode::InvalidFormat);
}

TEST(Archive, KeepsTheTextProgram) {
    Project project = blink();
    project.language = ProgramLanguage::Python;
    project.code = "while True:\n    digitalWrite(13, HIGH)\n";
    const auto bytes = toArchive(project);
    ASSERT_TRUE(bytes.ok());
    const auto reopened = fromArchive(bytes.value());
    ASSERT_TRUE(reopened.ok()) << reopened.error().detail;
    EXPECT_EQ(reopened.value().language, ProgramLanguage::Python);
    EXPECT_EQ(reopened.value().code, project.code);
    EXPECT_EQ(toArchive(reopened.value()).value(), bytes.value());
}

TEST(Archive, StoresTheCodeInItsOwnFiles) {
    struct Case {
        ProgramLanguage language;
        const char* myCode;
        const char* program;
    };
    for (const Case& c : {Case{ProgramLanguage::Cpp, "my_code.cpp", "program.cpp"},
                          Case{ProgramLanguage::Python, "my_code.py", "program.py"},
                          Case{ProgramLanguage::JavaScript, "my_code.js", "program.js"}}) {
        Project withMyCode = blink();
        withMyCode.userCode = UserCode{c.language, "// mon code été\nint vitesse = 3;\n"};
        const auto bytes = toArchive(withMyCode);
        ASSERT_TRUE(bytes.ok()) << bytes.error().detail;
        EXPECT_EQ(entryOf(bytes.value(), c.myCode), withMyCode.userCode->code);
        const std::string json = entryOf(bytes.value(), "project.json").value();
        EXPECT_NE(json.find(std::string("\"file\": \"") + c.myCode + "\""), std::string::npos) << json;
        EXPECT_EQ(json.find("vitesse"), std::string::npos) << "the code is not in project.json";
        EXPECT_NE(json.find("\"formatVersion\": 3"), std::string::npos);
        const auto reopened = fromArchive(bytes.value());
        ASSERT_TRUE(reopened.ok()) << reopened.error().detail;
        EXPECT_EQ(reopened.value().userCode->language, c.language);
        EXPECT_EQ(reopened.value().userCode->code, withMyCode.userCode->code);
        EXPECT_EQ(toArchive(reopened.value()).value(), bytes.value());

        Project text = blink();
        text.language = c.language;
        text.code = "boucle();\n";
        const auto textBytes = toArchive(text);
        ASSERT_TRUE(textBytes.ok());
        EXPECT_EQ(entryOf(textBytes.value(), c.program), text.code);
        EXPECT_FALSE(entryOf(textBytes.value(), c.myCode)) << "no My code file without My code";
        const auto textReopened = fromArchive(textBytes.value());
        ASSERT_TRUE(textReopened.ok()) << textReopened.error().detail;
        EXPECT_EQ(textReopened.value().code, text.code);
    }
}

TEST(Archive, BlockProjectsStayInVersion1WithTheAppVersion) {
    const auto bytes = toArchive(blink());
    ASSERT_TRUE(bytes.ok());
    const std::string json = entryOf(bytes.value(), "project.json").value();
    EXPECT_NE(json.find("\"formatVersion\": 1"), std::string::npos) << "older versions still open them";
    EXPECT_NE(json.find("\"savedWith\": \""), std::string::npos);
    const auto reopened = fromArchive(bytes.value());
    ASSERT_TRUE(reopened.ok());
    EXPECT_FALSE(reopened.value().savedWith.empty());
    EXPECT_EQ(toJson(reopened.value()).find("savedWith"), std::string::npos) << "only in archives";
}

TEST(Archive, OpensVersion2ArchivesWithTheCodeInline) {
    Project project = blink();
    project.userCode = UserCode{ProgramLanguage::Cpp, "int x = 1;\n"};
    const auto reopened = fromArchive(zipWith("project.json", toJson(project))); // as saved by 1.0
    ASSERT_TRUE(reopened.ok()) << reopened.error().detail;
    EXPECT_EQ(reopened.value().userCode->code, project.userCode->code);
    EXPECT_TRUE(reopened.value().savedWith.empty());
}

TEST(Archive, ReportsAMissingCodeFile) {
    const std::string json = R"({"format": "scratch2arduino-project", "formatVersion": 3, "board": "uno",
        "userCode": {"language": "python", "file": "my_code.py"}})";
    const auto missing = fromArchive(zipWith("project.json", json));
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::InvalidFormat);
    EXPECT_NE(missing.error().detail.find("my_code.py"), std::string::npos);
    EXPECT_FALSE(fromArchive(zipOf({{"project.json", json}, {"assets/my_code.py", "x"}})).ok());
    EXPECT_TRUE(fromArchive(zipOf({{"project.json", json}, {"my_code.py", "x = 1\n"}})).ok());
    EXPECT_EQ(fromJson(json).error().code, ErrorCode::InvalidFormat) << "a bare project.json has no files";
}

// Audit 2026-11-24 (#189): a thumbnail too large made Save (and autosave) fail, although
// loading ignores it. It is left out instead: the project is what matters.
TEST(Archive, LeavesOutAThumbnailTooLarge) {
    Project project = blink();
    project.thumbnail = std::string(s2a::project::kMaxThumbnailSize + 1, 'x');
    const auto bytes = toArchive(project);
    ASSERT_TRUE(bytes.ok()) << bytes.error().detail;
    EXPECT_FALSE(entryOf(bytes.value(), "thumbnail.png"));
}

// Audit 2026-11-24 (#189): two variables with the same id became two declarations of the same
// global, a raw compiler error. The first one is kept.
TEST(ProjectJson, KeepsTheFirstOfTwoVariablesOrFunctionsWithTheSameId) {
    const auto parsed = fromJson(R"({"format": "scratch2arduino-project", "formatVersion": 1, "name": "x", "board": "uno",
        "variables": [{"id": "v", "name": "score", "initialValue": 1}, {"id": "v", "name": "other", "initialValue": 2}],
        "functions": [{"id": "f", "name": "blink"}, {"id": "f", "name": "again"}],
        "scripts": []})");
    ASSERT_TRUE(parsed.ok()) << parsed.error().detail;
    ASSERT_EQ(parsed.value().variables.size(), 1U);
    EXPECT_EQ(parsed.value().variables[0].name, "score");
    ASSERT_EQ(parsed.value().functions.size(), 1U);
    EXPECT_EQ(parsed.value().functions[0].name, "blink");
}

TEST(Archive, KeepsTheThumbnail) {
    Project project = blink();
    project.thumbnail = std::string("\x89PNG\r\n\x1a\n\0data", 13);
    const auto bytes = toArchive(project);
    ASSERT_TRUE(bytes.ok()) << bytes.error().detail;
    EXPECT_EQ(entryOf(bytes.value(), "thumbnail.png"), project.thumbnail);
    const auto reopened = fromArchive(bytes.value());
    ASSERT_TRUE(reopened.ok());
    EXPECT_EQ(reopened.value().thumbnail, project.thumbnail);
    EXPECT_EQ(toArchive(reopened.value()).value(), bytes.value());
    EXPECT_FALSE(entryOf(toArchive(blink()).value(), "thumbnail.png")) << "no thumbnail, no entry";

    project.thumbnail = std::string(kMaxThumbnailSize + 1, 'x');
    EXPECT_FALSE(entryOf(toArchive(project).value(), "thumbnail.png")) << "too large: left out, the save works";
    const auto tooBig = fromArchive(zipOf({{"project.json", toJson(blink())}, {"thumbnail.png", project.thumbnail}}));
    ASSERT_TRUE(tooBig.ok()) << "only a picture: ignored";
    EXPECT_TRUE(tooBig.value().thumbnail.empty());
}

TEST(ProjectFile, ReadsOnlyTheThumbnail) {
    const TempDir dir;
    Project project = blink();
    project.thumbnail = "png bytes";
    ASSERT_TRUE(save(project, dir.path() / "a.s2a").ok());
    ASSERT_TRUE(save(blink(), dir.path() / "b.s2a").ok());
    EXPECT_EQ(thumbnailOf(dir.path() / "a.s2a"), "png bytes");
    EXPECT_EQ(thumbnailOf(dir.path() / "b.s2a"), "");
    EXPECT_EQ(thumbnailOf(dir.path() / "missing.s2a"), "");
}

TEST(CodeFileName, FollowsTheLanguage) {
    EXPECT_EQ(codeFileName(CodeFile::MyCode, ProgramLanguage::Python), "my_code.py");
    EXPECT_EQ(codeFileName(CodeFile::Program, ProgramLanguage::Cpp), "program.cpp");
    EXPECT_EQ(codeFileName(CodeFile::Program, ProgramLanguage::Blocks), "");
}

// Editing must never be blocked: incomplete projects and unknown blocks load fine.
TEST(ProjectJson, AcceptsIncompleteProjects) {
    const auto parsed = fromJson(R"({
        "format": "scratch2arduino-project", "formatVersion": 1, "board": "uno",
        "scripts": [{"blocks": [{"id": "a", "type": "block_from_the_future", "inputs": {"X": {}}}]}]
    })");
    ASSERT_TRUE(parsed.ok()) << parsed.error().detail;
    EXPECT_EQ(parsed.value().name, "");
    EXPECT_EQ(parsed.value().scripts[0].blocks[0].type, "block_from_the_future");
    EXPECT_FALSE(parsed.value().scripts[0].blocks[0].inputs.at("X").block);
}

TEST(ProjectJson, RejectsNewerFormats) {
    const auto parsed = fromJson(R"({"format": "scratch2arduino-project", "formatVersion": 99, "board": "uno"})");
    ASSERT_FALSE(parsed.ok());
    EXPECT_EQ(parsed.error().code, ErrorCode::UnsupportedVersion);
}

TEST(ProjectJson, RejectsInvalidDocuments) {
    EXPECT_EQ(fromJson("{oops").error().code, ErrorCode::InvalidJson);
    EXPECT_EQ(fromJson(R"({"format": "something-else", "formatVersion": 1})").error().code,
              ErrorCode::InvalidFormat);
    EXPECT_EQ(fromJson(R"([1])").error().code, ErrorCode::InvalidFormat);

    const auto badType = fromJson(R"({"format": "scratch2arduino-project", "formatVersion": 1, "board": "uno",
        "scripts": [{"blocks": [{"id": "a", "type": 3}]}]})");
    ASSERT_FALSE(badType.ok());
    EXPECT_EQ(badType.error().code, ErrorCode::InvalidFormat);
    EXPECT_NE(badType.error().detail.find("$.scripts[0].blocks[0].type"), std::string::npos)
        << badType.error().detail;

    EXPECT_FALSE(fromJson(R"({"format": "scratch2arduino-project", "formatVersion": 0, "board": "uno"})").ok());
    EXPECT_FALSE(fromJson(R"({"format": "scratch2arduino-project", "formatVersion": 1})").ok()); // no board
}

TEST(ProjectJson, RejectsAbsurdNesting) {
    std::string nested = R"({"id": "x", "type": "t"})";
    for (int i = 0; i < 300; ++i) {
        nested = R"({"id": "x", "type": "t", "inputs": {"A": {"block": )" + nested + "}}}";
    }
    const auto parsed = fromJson(R"({"format": "scratch2arduino-project", "formatVersion": 1, "board": "uno",
        "scripts": [{"blocks": [)" + nested + "]}]}");
    ASSERT_FALSE(parsed.ok());
    EXPECT_NE(parsed.error().detail.find("nested too deeply"), std::string::npos);
}

// --- .s2a archives -------------------------------------------------------------------------

TEST(Archive, SaveLoadSaveIsByteForByteIdentical) {
    Project project = blink();
    project.assets["images/logo.svg"] = "<svg/>";
    project.assets["binary.bin"] = std::string("\0\x01\xff", 3);

    const auto first = toArchive(project);
    ASSERT_TRUE(first.ok()) << first.error().detail;
    const auto loaded = fromArchive(first.value());
    ASSERT_TRUE(loaded.ok()) << loaded.error().detail;
    EXPECT_EQ(loaded.value().assets, project.assets);
    const auto second = toArchive(loaded.value());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.value(), second.value());
}

TEST(Archive, RejectsWhatIsNotAProject) {
    EXPECT_EQ(fromArchive("definitely not a zip").error().code, ErrorCode::InvalidArchive);
    EXPECT_EQ(fromArchive(zipWith("readme.txt", "hello")).error().code, ErrorCode::InvalidArchive);
    EXPECT_EQ(fromArchive(zipWith("project.json", "{")).error().code, ErrorCode::InvalidJson);
    EXPECT_EQ(fromArchive(zipWith("assets/../evil", "x")).error().code, ErrorCode::InvalidArchive);
}

TEST(Archive, KeepsLongAssetNames) {
    // Longer than miniz's fixed 512-byte m_filename buffer; two names sharing a long prefix.
    const std::string prefix = "images/" + std::string(600, 'a');
    Project project = blink();
    project.assets[prefix + "1.png"] = "one";
    project.assets[prefix + "2.png"] = "two";
    const auto bytes = toArchive(project);
    ASSERT_TRUE(bytes.ok()) << bytes.error().detail;
    const auto loaded = fromArchive(bytes.value());
    ASSERT_TRUE(loaded.ok()) << loaded.error().detail;
    EXPECT_EQ(loaded.value().assets, project.assets);
}

TEST(Archive, RefusesToSaveWhatCouldNotBeReopened) {
    Project project = blink();
    project.assets["huge.bin"] = std::string(65ULL * 1024 * 1024, 'x'); // over the 64 MiB entry limit
    const auto bytes = toArchive(project);
    ASSERT_FALSE(bytes.ok());
    EXPECT_EQ(bytes.error().code, ErrorCode::WriteFailed);

    // save() fails before touching an existing file.
    const TempDir dir;
    const fs::path path = dir.path() / "existing.s2a";
    ASSERT_TRUE(save(blink(), path).ok());
    EXPECT_EQ(save(project, path).error().code, ErrorCode::WriteFailed);
    EXPECT_TRUE(load(path).ok());
}

TEST(Archive, RejectsUnsafeAssetNames) {
    for (const char* name : {"../escape", "/absolute", "a/../../b", "dir/", "C:evil", "back\\slash", ""}) {
        Project project = blink();
        project.assets[name] = "x";
        EXPECT_FALSE(toArchive(project).ok()) << name;
    }
}

TEST(ProjectFile, SavesAndLoadsWithUnicodePaths) {
    const TempDir dir;
    const fs::path path = dir.path() / fs::u8path("projet été ✓.s2a");
    ASSERT_TRUE(save(blink(), path).ok());
    EXPECT_FALSE(fs::exists(fs::path(path).concat(".tmp")));
    const auto loaded = load(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error().detail;
    EXPECT_EQ(toJson(loaded.value()), toJson(blink()));

    // Saving again over an existing file replaces it.
    Project renamed = blink();
    renamed.name = "Nouveau nom";
    ASSERT_TRUE(save(renamed, path).ok());
    EXPECT_EQ(load(path).value().name, "Nouveau nom");
}

TEST(ProjectFile, ReportsMissingAndUnwritableFiles) {
    const TempDir dir;
    EXPECT_EQ(load(dir.path() / "missing.s2a").error().code, ErrorCode::FileNotFound);
    EXPECT_EQ(save(blink(), dir.path() / "no-such-dir" / "p.s2a").error().code, ErrorCode::WriteFailed);

    const fs::path garbage = dir.path() / "garbage.s2a";
    std::ofstream(garbage) << "garbage";
    EXPECT_EQ(load(garbage).error().code, ErrorCode::InvalidArchive);
}

TEST(ProjectFile, OpensABareProjectJson) {
    const auto path = fs::temp_directory_path() / "s2a-bare-project.json";
    {
        std::ofstream file(path, std::ios::binary);
        file << toJson(blink());
    }
    const auto loaded = load(path);
    fs::remove(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error().detail;
    EXPECT_EQ(loaded.value().name, blink().name);
    EXPECT_EQ(loaded.value().scripts.size(), blink().scripts.size());
}

} // namespace
