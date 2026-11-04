#include "s2a/process.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace {

using namespace std::chrono_literals;
using s2a::process::Command;

Command helper(std::vector<std::string> args) {
    Command command;
    command.program = std::filesystem::u8path(S2A_PROCESS_HELPER);
    command.args = std::move(args);
    return command;
}

TEST(Process, CapturesStdoutAndExitCode) {
    const auto result = s2a::process::run(helper({"echo", "one", "deux mots", "été"}));
    ASSERT_TRUE(result.ok()) << result.error().detail;
    EXPECT_EQ(result.value().exitCode, 0);
    EXPECT_TRUE(result.value().succeeded());
    std::string out = result.value().out;
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
    EXPECT_EQ(out, "one\ndeux mots\nété\n");
}

TEST(Process, ReportsNonZeroExitCodesAsResults) {
    const auto result = s2a::process::run(helper({"exit", "3"}));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().exitCode, 3);
    EXPECT_FALSE(result.value().succeeded());
}

TEST(Process, CapturesStderr) {
    const auto result = s2a::process::run(helper({"stderr", "oops"}));
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().err, "oops");
    EXPECT_TRUE(result.value().out.empty());
}

TEST(Process, PassesEnvironmentVariables) {
    Command command = helper({"env", "S2A_TEST_VARIABLE"});
    command.env["S2A_TEST_VARIABLE"] = "valeur";
    const auto result = s2a::process::run(command);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().out, "valeur");
}

TEST(Process, UsesTheWorkingDirectory) {
    Command command = helper({"pwd"});
    command.workingDirectory = std::filesystem::temp_directory_path();
    const auto result = s2a::process::run(command);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(std::filesystem::canonical(std::filesystem::u8path(result.value().out)),
              std::filesystem::canonical(command.workingDirectory));
}

TEST(Process, StopsProgramsThatRunTooLong) {
    Command command = helper({"sleep", "10000"});
    command.timeout = 300ms;
    const auto start = std::chrono::steady_clock::now();
    const auto result = s2a::process::run(command);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().timedOut);
    EXPECT_FALSE(result.value().succeeded());
    EXPECT_LT(std::chrono::steady_clock::now() - start, 8s);
}

TEST(Process, StopsProgramsThatCloseTheirOutputsAndKeepRunning) {
    Command command = helper({"quiet-sleep", "10000"});
    command.timeout = 300ms;
    const auto start = std::chrono::steady_clock::now();
    const auto result = s2a::process::run(command);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().timedOut);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 8s);
}

TEST(Process, OverridesInheritedEnvironmentVariables) {
#ifdef _WIN32
    _putenv_s("S2A_TEST_OVERRIDE", "parent");
#else
    setenv("S2A_TEST_OVERRIDE", "parent", 1); // NOLINT(concurrency-mt-unsafe)
#endif
    Command command = helper({"env", "S2A_TEST_OVERRIDE"});
    command.env["S2A_TEST_OVERRIDE"] = "child";
    const auto result = s2a::process::run(command);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().out, "child");
    // Variables that are not overridden are still inherited.
    const auto inherited = s2a::process::run(helper({"env", "S2A_TEST_OVERRIDE"}));
    ASSERT_TRUE(inherited.ok());
    EXPECT_EQ(inherited.value().out, "parent");
}

TEST(Process, FailsWhenTheProgramDoesNotExist) {
    Command command;
    command.program = std::filesystem::temp_directory_path() / "s2a-no-such-program";
    const auto result = s2a::process::run(command);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, s2a::ErrorCode::ProcessFailed);
}

TEST(Process, FindsProgramsInThePath) {
#ifdef _WIN32
    EXPECT_FALSE(s2a::process::findInPath("cmd").empty());
#else
    EXPECT_FALSE(s2a::process::findInPath("sh").empty());
#endif
    EXPECT_TRUE(s2a::process::findInPath("s2a-certainly-not-installed").empty());
}

#ifndef _WIN32
TEST(Process, SkipsFilesThatAreNotExecutableInThePath) {
    namespace fs = std::filesystem;
    const fs::path first = fs::temp_directory_path() / "s2a-path-first";
    const fs::path second = fs::temp_directory_path() / "s2a-path-second";
    fs::create_directories(first);
    fs::create_directories(second);
    for (const fs::path& directory : {first, second}) {
        std::ofstream(directory / "s2a-tool") << "#!/bin/sh\n";
    }
    fs::permissions(first / "s2a-tool", fs::perms::owner_read | fs::perms::owner_write);
    fs::permissions(second / "s2a-tool", fs::perms::owner_all);

    const char* saved = std::getenv("PATH"); // NOLINT(concurrency-mt-unsafe)
    const std::string original = saved != nullptr ? saved : "";
    setenv("PATH", (first.string() + ":" + second.string()).c_str(), 1); // NOLINT(concurrency-mt-unsafe)
    const fs::path found = s2a::process::findInPath("s2a-tool");
    setenv("PATH", original.c_str(), 1); // NOLINT(concurrency-mt-unsafe)
    fs::remove_all(first);
    fs::remove_all(second);
    EXPECT_EQ(found, second / "s2a-tool");
}
#endif

TEST(Process, KnowsTheExecutableDirectory) {
    const auto directory = s2a::process::executableDirectory();
    ASSERT_FALSE(directory.empty());
    EXPECT_TRUE(std::filesystem::is_directory(directory));
}

} // namespace
