#include "s2a/arduino_cli.hpp"

#include <gtest/gtest.h>
#include <string>

namespace {

namespace fs = std::filesystem;
using s2a::process::Command;
using s2a::process::Output;
using s2a::toolchain::ArduinoCli;
using s2a::toolchain::Directories;

/// Records the commands it receives and replies with canned outputs, in order.
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

const Directories kDirectories{"/data", "/downloads", "/user"};

const s2a::Board& uno() {
    return *s2a::BoardDatabase::builtin().find("uno");
}

// Outputs captured from arduino-cli 1.5.1.
constexpr std::string_view kBoardList = R"j({
  "detected_ports": [
    {"matching_boards": [{"name": "Arduino Uno", "fqbn": "arduino:avr:uno"}],
     "port": {"address": "/dev/ttyACM0", "label": "/dev/ttyACM0", "protocol": "serial",
              "protocol_label": "Serial Port (USB)", "properties": {"pid": "0x0043", "vid": "0x2341"}}},
    {"port": {"address": "/dev/ttyUSB0", "label": "/dev/ttyUSB0", "protocol": "serial",
              "protocol_label": "Serial Port (USB)", "properties": {"pid": "0x7523", "vid": "0x1A86"}}},
    {"port": {"address": "/dev/ttyS0", "label": "/dev/ttyS0", "protocol": "serial", "protocol_label": "Serial Port"}}
  ]
})j";

constexpr std::string_view kCompileSuccess = R"j({
  "compiler_out": "Sketch uses 924 bytes (2%) of program storage space. Maximum is 32256 bytes.\n",
  "compiler_err": "",
  "builder_result": {"build_path": "/tmp/b",
    "executable_sections_size": [{"name": "text", "size": 924, "max_size": 32256},
                                 {"name": "data", "size": 9, "max_size": 2048}]},
  "upload_result": {}, "success": true
})j";

constexpr std::string_view kCompileFailure = R"j({
  "compiler_out": "",
  "compiler_err": "/s/bad/bad.ino:1:10: fatal error: Servo.h: No such file or directory\n",
  "builder_result": {}, "upload_result": {}, "success": false,
  "error": "Error during build: exit status 1"
})j";

constexpr std::string_view kUploadFailure = R"j({
  "error": "Failed uploading: uploading error: exit status 1",
  "output": {"stdout": "", "stderr": "OS error: cannot open port /dev/ttyNOPE: No such file or directory\n"}
})j";

TEST(ArduinoCli, PassesPrivateDirectoriesAndJsonFlags) {
    FakeRunner fake;
    fake.replies.push_back(reply(R"j({"Application": "arduino-cli", "VersionString": "1.5.1"})j"));
    const ArduinoCli cli("/opt/arduino-cli", kDirectories, fake.runner());
    const auto version = cli.version();
    ASSERT_TRUE(version.ok()) << version.error().detail;
    EXPECT_EQ(version.value(), "1.5.1");

    ASSERT_EQ(fake.commands.size(), 1U);
    const Command& command = fake.commands[0];
    EXPECT_EQ(command.program, fs::path("/opt/arduino-cli"));
    EXPECT_EQ(command.args, (std::vector<std::string>{"version", "--json", "--no-color"}));
    EXPECT_EQ(command.env.at("ARDUINO_DIRECTORIES_DATA"), fs::path("/data").u8string());
    EXPECT_EQ(command.env.at("ARDUINO_DIRECTORIES_USER"), fs::path("/user").u8string());
    EXPECT_EQ(command.env.at("ARDUINO_UPDATER_ENABLE_NOTIFICATION"), "false");
    EXPECT_GT(command.timeout.count(), 0);
}

TEST(ArduinoCli, ReportsWhenArduinoCliCannotRun) {
    FakeRunner fake; // no reply: the runner fails
    const ArduinoCli cli("/missing", kDirectories, fake.runner());
    EXPECT_EQ(cli.version().error().code, s2a::ErrorCode::ProcessFailed);
}

TEST(ArduinoCli, ChecksInstalledPlatforms) {
    FakeRunner fake;
    const std::string installed =
        R"j({"platforms": [{"id": "arduino:avr", "installed_version": "1.8.8", "latest_version": "1.8.8"}]})j";
    fake.replies = {reply(installed), reply(installed), reply(R"j({"platforms": []})j")};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    EXPECT_TRUE(cli.isPlatformInstalled("arduino:avr").value());
    EXPECT_FALSE(cli.isPlatformInstalled("esp32:esp32").value());
    EXPECT_FALSE(cli.isPlatformInstalled("arduino:avr").value());
}

TEST(ArduinoCli, InstallsPlatformsAfterUpdatingTheIndex) {
    FakeRunner fake;
    fake.replies = {reply("{}"), reply("{}")};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto outcome = cli.installPlatform("arduino:avr");
    ASSERT_TRUE(outcome.ok());
    EXPECT_TRUE(outcome.value().success);
    ASSERT_EQ(fake.commands.size(), 2U);
    EXPECT_EQ(fake.commands[0].args.at(1), "update-index");
    EXPECT_EQ(fake.commands[1].args.at(2), "arduino:avr");
}

TEST(ArduinoCli, StopsInstallingWhenTheIndexCannotBeDownloaded) {
    FakeRunner fake;
    fake.replies = {reply(R"j({"error": "dial tcp: lookup downloads.arduino.cc: no such host"})j", 1)};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto outcome = cli.installPlatform("arduino:avr");
    ASSERT_TRUE(outcome.ok());
    EXPECT_FALSE(outcome.value().success);
    EXPECT_NE(outcome.value().error.find("no such host"), std::string::npos);
    EXPECT_EQ(fake.commands.size(), 1U);
}

TEST(ArduinoCli, DetectsPortsAndBoards) {
    FakeRunner fake;
    fake.replies = {reply(std::string(kBoardList))};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto ports = cli.detectPorts();
    ASSERT_TRUE(ports.ok()) << ports.error().detail;
    ASSERT_EQ(ports.value().size(), 3U);

    const auto& unoPort = ports.value()[0];
    EXPECT_EQ(unoPort.address, "/dev/ttyACM0");
    EXPECT_EQ(unoPort.protocolLabel, "Serial Port (USB)");
    ASSERT_TRUE(unoPort.usb.has_value());
    EXPECT_EQ(unoPort.usb->vendor, 0x2341);
    EXPECT_EQ(unoPort.boardId, "uno");

    EXPECT_EQ(ports.value()[1].boardId, "nano"); // CH340 clone: recognised by USB id only
    EXPECT_EQ(ports.value()[2].boardId, "");     // built-in serial port
    EXPECT_FALSE(ports.value()[2].usb.has_value());
}

TEST(ArduinoCli, CompilesWithTheBoardFqbn) {
    FakeRunner fake;
    fake.replies = {reply(std::string(kCompileSuccess))};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto outcome = cli.compile("/sketches/blink", uno(), "/build/blink");
    ASSERT_TRUE(outcome.ok());
    EXPECT_TRUE(outcome.value().success);
    ASSERT_EQ(outcome.value().memory.size(), 2U);
    EXPECT_EQ(outcome.value().memory[0].section, "text");
    EXPECT_EQ(outcome.value().memory[0].size, 924U);
    EXPECT_EQ(outcome.value().memory[1].max, 2048U);
    EXPECT_NE(outcome.value().out.find("924 bytes"), std::string::npos);

    const auto& args = fake.commands.at(0).args;
    EXPECT_EQ(args.at(0), "compile");
    EXPECT_EQ(args.at(2), "arduino:avr:uno");
    EXPECT_EQ(args.at(4), fs::path("/build/blink").u8string());
}

TEST(ArduinoCli, ReportsCompilationFailures) {
    FakeRunner fake;
    fake.replies = {reply(std::string(kCompileFailure), 1)};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto outcome = cli.compile("/s/bad", uno(), "/b");
    ASSERT_TRUE(outcome.ok());
    EXPECT_FALSE(outcome.value().success);
    EXPECT_NE(outcome.value().err.find("Servo.h"), std::string::npos);
    EXPECT_EQ(outcome.value().error, "Error during build: exit status 1");
}

TEST(ArduinoCli, UploadsToThePort) {
    FakeRunner fake;
    fake.replies = {reply(std::string(kUploadFailure), 1)};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto outcome = cli.upload("/s/blink", uno(), "/b", "/dev/ttyNOPE");
    ASSERT_TRUE(outcome.ok());
    EXPECT_FALSE(outcome.value().success);
    EXPECT_NE(outcome.value().err.find("cannot open port"), std::string::npos);
    const auto& args = fake.commands.at(0).args;
    EXPECT_EQ(args.at(0), "upload");
    EXPECT_EQ(args.at(4), "/dev/ttyNOPE");
}

TEST(ArduinoCli, ReadsFatalErrorsWrittenToStderr) {
    FakeRunner fake;
    Output failure = reply("", 1);
    failure.err = R"j({"error": "Error during Upload: Failed uploading: port not found"})j";
    fake.replies = {failure};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto outcome = cli.upload("/s", uno(), "/b", "COM9");
    ASSERT_TRUE(outcome.ok());
    EXPECT_FALSE(outcome.value().success);
    EXPECT_EQ(outcome.value().error, "Error during Upload: Failed uploading: port not found");
    EXPECT_EQ(outcome.value().err.find('{'), std::string::npos);
}

TEST(ArduinoCli, KeepsUploaderMessagesOnSuccess) {
    FakeRunner fake;
    fake.replies = {reply(R"j({"stdout": "", "stderr": "avrdude: 924 bytes of flash verified\n",
                              "updated_upload_port": null})j")};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto outcome = cli.upload("/s", uno(), "/b", "COM3");
    ASSERT_TRUE(outcome.ok());
    EXPECT_TRUE(outcome.value().success);
    EXPECT_NE(outcome.value().err.find("verified"), std::string::npos);
}

TEST(ArduinoCli, KeepsRawOutputWhenItIsNotJson) {
    FakeRunner fake;
    Output crash = reply("panic: something", 2);
    crash.err = "goroutine 1";
    fake.replies = {crash};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto outcome = cli.upload("/s", uno(), "/b", "COM3");
    ASSERT_TRUE(outcome.ok());
    EXPECT_FALSE(outcome.value().success);
    EXPECT_EQ(outcome.value().out, "panic: something");
    EXPECT_EQ(outcome.value().err, "goroutine 1");
}

TEST(Directories, ArePrivateToScratch2Arduino) {
    const auto directories = Directories::defaults();
    EXPECT_NE(directories.data.u8string().find("Scratch2Arduino"), std::string::npos);
    EXPECT_EQ(directories.data.parent_path(), directories.user.parent_path());
}

#ifdef S2A_BUNDLED_ARDUINO_CLI
// The arduino-cli downloaded by the build really runs (no network needed).
TEST(BundledArduinoCli, ReportsItsVersion) {
    const fs::path temp = fs::temp_directory_path() / "s2a-arduino-cli-test";
    const ArduinoCli cli(fs::u8path(S2A_BUNDLED_ARDUINO_CLI), {temp / "data", temp / "downloads", temp / "user"});
    const auto version = cli.version();
    ASSERT_TRUE(version.ok()) << version.error().detail;
    EXPECT_EQ(version.value(), S2A_BUNDLED_ARDUINO_CLI_VERSION);
}
#endif

TEST(ArduinoCli, ListsAndInstallsLibraries) {
    FakeRunner fake;
    fake.replies = {reply(R"({"installed_libraries":[{"library":{"name":"Servo","version":"1.3.0"}}]})"),
                    reply(R"({"installed_libraries":[]})"), reply("{}"), reply("{}")};
    const ArduinoCli cli("arduino-cli", kDirectories, fake.runner());
    const auto installed = cli.installedLibraries();
    ASSERT_TRUE(installed.ok());
    EXPECT_EQ(installed.value(), std::vector<std::string>{"Servo"});
    const auto missing = s2a::toolchain::missingLibraries(cli, {"Servo"});
    ASSERT_TRUE(missing.ok());
    EXPECT_EQ(missing.value(), std::vector<std::string>{"Servo"});
    const auto outcome = cli.installLibraries(missing.value());
    ASSERT_TRUE(outcome.ok());
    EXPECT_TRUE(outcome.value().success);
    ASSERT_EQ(fake.commands.size(), 4U);
    EXPECT_EQ(fake.commands[2].args.at(1), "update-index");
    EXPECT_EQ(fake.commands[3].args.at(2), "Servo");
}

TEST(ArduinoCli, FindsTheLibrariesASketchNeeds) {
    using s2a::toolchain::requiredLibraries;
    EXPECT_TRUE(requiredLibraries("void setup() {}\n").empty());
    EXPECT_EQ(requiredLibraries("#include <Servo.h>\n#include \"Servo.h\"\n#include <Wire.h>\n"),
              std::vector<std::string>{"Servo"});
    EXPECT_TRUE(requiredLibraries("// #include <Servo2.h>").empty());
}

} // namespace
