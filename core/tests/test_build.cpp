#include "s2a/build.hpp"

#include "s2a/codegen.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using s2a::Language;
using s2a::process::Command;
using s2a::process::Output;
using s2a::toolchain::ArduinoCli;
using s2a::toolchain::BuildOptions;
using s2a::toolchain::BuildStep;
using namespace s2a::project;

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

Output reply(std::string out, int exitCode = 0) {
    Output output;
    output.out = std::move(out);
    output.exitCode = exitCode;
    return output;
}

const std::string kInstalled =
    R"j({"platforms": [{"id": "arduino:avr", "installed_version": "1.8.8", "latest_version": "1.8.8"}]})j";
const std::string kCompileSuccess = R"j({
  "compiler_out": "Sketch uses 924 bytes (2%) of program storage space.\n", "compiler_err": "",
  "builder_result": {"build_path": "/tmp/b",
    "executable_sections_size": [{"name": "text", "size": 924, "max_size": 32256},
                                 {"name": "data", "size": 9, "max_size": 2048}]},
  "upload_result": {}, "success": true})j";

Project blink() {
    Project project;
    project.name = "Clignotant été";
    project.boardId = "uno";
    Block on;
    on.id = "on";
    on.type = "pins_builtin_led";
    on.fields["STATE"] = "ON";
    Block wait;
    wait.id = "wait";
    wait.type = "control_wait";
    wait.inputs["SECONDS"].value = "1";
    Block forever;
    forever.id = "forever";
    forever.type = "control_forever";
    forever.statements["SUBSTACK"] = {on, wait};
    Block hat;
    hat.id = "hat";
    hat.type = "events_when_program_starts";
    project.scripts.push_back({0, 0, {hat, forever}});
    return project;
}

class Build : public ::testing::Test {
protected:
    void SetUp() override {
        options.workDirectory = fs::temp_directory_path() / ("s2a-build-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::remove_all(options.workDirectory);
    }
    void TearDown() override { fs::remove_all(options.workDirectory); }

    ArduinoCli cli() { return {"arduino-cli", {"/data", "/downloads", "/user"}, fake.runner()}; }

    FakeRunner fake;
    BuildOptions options;
};

TEST_F(Build, WritesTheSketchAndCompiles) {
    fake.replies = {reply(kInstalled), reply(kCompileSuccess)};
    std::vector<BuildStep> steps;
    options.progress = [&steps](BuildStep step) { steps.push_back(step); };

    const auto result = s2a::toolchain::buildProject(cli(), blink(), Language::English, options);
    ASSERT_TRUE(result.ok) << result.message << "\n" << result.details;
    EXPECT_NE(result.message.find("2%"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("Arduino Uno"), std::string::npos);
    EXPECT_EQ(steps, std::vector<BuildStep>{BuildStep::Compiling});

    const fs::path ino = options.workDirectory / "sketches" / "Clignotant_ete" / "Clignotant_ete.ino";
    std::ifstream file(ino);
    std::stringstream content;
    content << file.rdbuf();
    EXPECT_EQ(content.str(), s2a::codegen::generate(blink()).source);
    ASSERT_EQ(fake.commands.size(), 2U);
    EXPECT_EQ(fake.commands[1].args.at(0), "compile");
}

TEST_F(Build, InstallsTheBoardToolsFirstWhenMissing) {
    fake.replies = {reply(R"j({"platforms": []})j"), reply("{}"), reply("{}"), reply(kCompileSuccess)};
    std::vector<BuildStep> steps;
    options.progress = [&steps](BuildStep step) { steps.push_back(step); };
    const auto result = s2a::toolchain::buildProject(cli(), blink(), Language::French, options);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(steps, (std::vector<BuildStep>{BuildStep::InstallingTools, BuildStep::Compiling}));
}

TEST_F(Build, PointsCompilerErrorsAtBlocks) {
    const auto sketch = s2a::codegen::generate(blink());
    const int line = sketch.blockLines.at("wait");
    const std::string failure = R"j({"compiler_out": "", "compiler_err": "/w/Clignotant_ete/Clignotant_ete.ino:)j" +
                                std::to_string(line) +
                                R"j(:3: error: 'oops' was not declared in this scope\n",
      "builder_result": {}, "upload_result": {}, "success": false, "error": "exit status 1"})j";
    fake.replies = {reply(kInstalled), reply(failure, 1)};
    const auto result = s2a::toolchain::buildProject(cli(), blink(), Language::French, options);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.message.empty());
    EXPECT_EQ(result.blockIds, std::vector<std::string>{"wait"});
}

TEST_F(Build, UploadsWhenAPortIsGiven) {
    fake.replies = {reply(kInstalled), reply(kCompileSuccess), reply("{}")};
    options.port = "/dev/ttyACM0";
    const auto result = s2a::toolchain::buildProject(cli(), blink(), Language::French, options);
    ASSERT_TRUE(result.ok) << result.message << "\n" << result.details;
    ASSERT_EQ(fake.commands.size(), 3U);
    EXPECT_EQ(fake.commands[2].args.at(0), "upload");
    EXPECT_NE(result.message.find("Arduino Uno"), std::string::npos);
}

TEST_F(Build, StopsBeforeCompilingWhenTheBlocksHaveMistakes) {
    Project project = blink();
    Block condition;
    condition.id = "if";
    condition.type = "control_if"; // no condition
    project.scripts[0].blocks.insert(project.scripts[0].blocks.begin() + 1, condition);
    const auto result = s2a::toolchain::buildProject(cli(), project, Language::English, options);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("condition"), std::string::npos) << result.message;
    EXPECT_EQ(result.blockIds, std::vector<std::string>{"if"});
    EXPECT_TRUE(fake.commands.empty()); // arduino-cli is not even started
}

TEST_F(Build, ReportsWarningsButStillBuilds) {
    Project project = blink();
    Block loose;
    loose.id = "loose";
    loose.type = "pins_builtin_led";
    project.scripts.push_back({300, 0, {loose}});
    fake.replies = {reply(kInstalled), reply(kCompileSuccess)};
    const auto result = s2a::toolchain::buildProject(cli(), project, Language::English, options);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.warnings.size(), 1U);
    EXPECT_EQ(result.warningBlockIds, std::vector<std::string>{"loose"});
}

TEST_F(Build, ExplainsAnUnknownBoard) {
    Project project = blink();
    project.boardId = "abacus";
    const auto result = s2a::toolchain::buildProject(cli(), project, Language::English, options);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("abacus"), std::string::npos);
    EXPECT_TRUE(fake.commands.empty());
}

} // namespace
