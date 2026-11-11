#include "s2a/diagnostics.hpp"

#include <gtest/gtest.h>
#include <string>

namespace {

using namespace s2a::toolchain;
using s2a::Language;

CompileOutcome failedCompile(std::string err, std::string error = "Error during build: exit status 1") {
    CompileOutcome outcome;
    outcome.success = false;
    outcome.err = std::move(err);
    outcome.error = std::move(error);
    return outcome;
}

CommandOutcome failedUpload(std::string err) {
    CommandOutcome outcome;
    outcome.success = false;
    outcome.err = std::move(err);
    outcome.error = "Failed uploading: uploading error: exit status 1";
    return outcome;
}

TEST(CompilerMessages, ParsesGccDiagnostics) {
    // Captured from arduino-cli 1.5.1 / avr-gcc.
    const std::string output =
        "/s/bad2/bad2.ino: In function 'void setup()':\n"
        "/s/bad2/bad2.ino:1:16: error: 'pinMod' was not declared in this scope\n"
        " void setup() { pinMod(13, OUTPUT) }\n"
        "                ^~~~~~\n"
        "/s/bad2/bad2.ino:1:16: note: suggested alternative: 'pinMode'\r\n"
        "C:\\Users\\Élève\\sketch\\sketch.ino:3:1: warning: unused variable 'x'\n";
    const auto messages = parseCompilerMessages(output);
    ASSERT_EQ(messages.size(), 3U);
    EXPECT_EQ(messages[0].file, "/s/bad2/bad2.ino");
    EXPECT_EQ(messages[0].line, 1);
    EXPECT_EQ(messages[0].column, 16);
    EXPECT_EQ(messages[0].severity, "error");
    EXPECT_EQ(messages[0].text, "'pinMod' was not declared in this scope");
    EXPECT_EQ(messages[1].severity, "note");
    EXPECT_EQ(messages[1].text, "suggested alternative: 'pinMode'");
    EXPECT_EQ(messages[2].file, "C:\\Users\\Élève\\sketch\\sketch.ino");
    EXPECT_EQ(messages[2].severity, "warning");
    EXPECT_TRUE(parseCompilerMessages("").empty());
}

TEST(DiagnoseCompile, SuccessIsNoProblem) {
    CompileOutcome outcome;
    outcome.success = true;
    EXPECT_EQ(diagnoseCompile(outcome).problem, Problem::None);
}

TEST(DiagnoseCompile, MissingLibrary) {
    const auto diagnosis = diagnoseCompile(
        failedCompile("/s/bad/bad.ino:1:10: fatal error: Servo.h: No such file or directory\n #include <Servo.h>\n"));
    EXPECT_EQ(diagnosis.problem, Problem::LibraryMissing);
    EXPECT_EQ(describe(diagnosis, Language::French),
              "Le programme utilise la bibliothèque \"Servo\", qui n'est pas installée.");
    EXPECT_EQ(describe(diagnosis, Language::English), "The program uses the \"Servo\" library, which is not installed.");
}

TEST(DiagnoseCompile, CompilerErrorsKeepOnlyErrors) {
    const auto diagnosis = diagnoseCompile(failedCompile(
        "/s/a.ino:1:16: error: 'pinMod' was not declared in this scope\n"
        "/s/a.ino:1:16: note: suggested alternative: 'pinMode'\n"
        "/s/a.ino:2:3: error: expected ';' before '}' token\n"));
    EXPECT_EQ(diagnosis.problem, Problem::CompilerError);
    ASSERT_EQ(diagnosis.messages.size(), 2U);
    EXPECT_EQ(diagnosis.messages[1].line, 2);
    EXPECT_EQ(describe(diagnosis, Language::French), "Le programme contient 2 erreur(s). Vérifie les blocs signalés.");
}

TEST(DiagnoseCompile, MissingPlatform) {
    const auto diagnosis = diagnoseCompile(failedCompile(
        "", "Error during build: Platform 'arduino:avr' not found: platform not installed\n"
            "Try running `arduino-cli core install arduino:avr`"));
    EXPECT_EQ(diagnosis.problem, Problem::PlatformMissing);
}

TEST(DiagnoseCompile, MemoryProblems) {
    EXPECT_EQ(diagnoseCompile(failedCompile("", "Sketch too big; see https://support.arduino.cc/... for tips")).problem,
              Problem::SketchTooBig);
    EXPECT_EQ(diagnoseCompile(failedCompile("", "text section exceeds available space in board")).problem,
              Problem::SketchTooBig);
    EXPECT_EQ(diagnoseCompile(failedCompile("", "Not enough memory; see https://support.arduino.cc/...")).problem,
              Problem::NotEnoughMemory);
}

TEST(DiagnoseCompile, TimeoutAndUnknown) {
    CompileOutcome timedOut = failedCompile("");
    timedOut.timedOut = true;
    EXPECT_EQ(diagnoseCompile(timedOut).problem, Problem::Timeout);
    const auto unknown = diagnoseCompile(failedCompile("something odd", "weird"));
    EXPECT_EQ(unknown.problem, Problem::Unknown);
    EXPECT_NE(unknown.technical.find("something odd"), std::string::npos);
}

TEST(DiagnoseUpload, PortProblems) {
    // Linux / avrdude, captured with arduino-cli 1.5.1.
    EXPECT_EQ(diagnoseUpload(failedUpload("OS error: cannot open port /dev/ttyNOPE: No such file or directory\n"),
                             "/dev/ttyNOPE")
                  .problem,
              Problem::PortNotFound);
    EXPECT_EQ(diagnoseUpload(failedUpload("avrdude: ser_open(): can't open device \"/dev/ttyACM0\": Permission denied"),
                             "/dev/ttyACM0")
                  .problem,
              Problem::PortPermissionDenied);
    EXPECT_EQ(diagnoseUpload(failedUpload("ser_open(): can't open device \"/dev/ttyACM0\": Device or resource busy"),
                             "/dev/ttyACM0")
                  .problem,
              Problem::PortBusy);
    // Windows.
    EXPECT_EQ(diagnoseUpload(failedUpload("avrdude: ser_open(): can't open device \"\\\\.\\COM3\": Access is denied."),
                             "COM3")
                  .problem,
              Problem::PortBusy);
    EXPECT_EQ(diagnoseUpload(failedUpload("can't open device \"\\\\.\\COM9\": The system cannot find the file specified."),
                             "COM9")
                  .problem,
              Problem::PortNotFound);
}

TEST(DiagnoseUpload, BoardNotResponding) {
    const auto diagnosis =
        diagnoseUpload(failedUpload("avrdude: stk500_recv(): programmer is not responding\n"
                                    "avrdude: stk500_getsync() attempt 10 of 10: not in sync: resp=0x00\n"),
                       "COM4");
    EXPECT_EQ(diagnosis.problem, Problem::BoardNotResponding);
    EXPECT_NE(describe(diagnosis, Language::French).find("COM4"), std::string::npos);
    EXPECT_NE(describe(diagnosis, Language::English).find("RESET"), std::string::npos);
}

TEST(DiagnoseInstall, NetworkProblems) {
    CommandOutcome outcome;
    outcome.success = false;
    outcome.error = "Error updating index: Get \"https://downloads.arduino.cc/packages/package_index.tar.bz2\": "
                    "dial tcp: lookup downloads.arduino.cc: no such host";
    EXPECT_EQ(diagnoseInstall(outcome).problem, Problem::NetworkUnavailable);
    outcome.success = true;
    EXPECT_EQ(diagnoseInstall(outcome).problem, Problem::None);
}

TEST(Describe, EveryProblemHasATranslation) {
    for (int value = static_cast<int>(Problem::None); value <= static_cast<int>(Problem::Unknown); ++value) {
        const auto key = problemMessageKey(static_cast<Problem>(value));
        for (const Language language : s2a::kLanguages) {
            EXPECT_TRUE(s2a::defaultTranslator().has(key, language)) << key;
        }
    }
}

TEST(Diagnostics, DownloadErrorsAreNotAlwaysNetworkProblems) {
    CommandOutcome outcome;
    outcome.error = "Error updating index: open /data/package_index.json: permission denied";
    EXPECT_EQ(s2a::toolchain::diagnoseInstall(outcome).problem, Problem::Unknown);
    outcome.error = "Error downloading index: Get \"https://downloads.arduino.cc\": dial tcp: lookup downloads.arduino.cc: no such host";
    EXPECT_EQ(s2a::toolchain::diagnoseInstall(outcome).problem, Problem::NetworkUnavailable);
}

TEST(Diagnostics, RecognisesLinkerMemoryOverflows) {
    s2a::toolchain::CompileOutcome outcome;
    outcome.err = "/usr/bin/avr-ld: sketch.ino.elf section `.text' will not fit in region `text'\n"
                  "/usr/bin/avr-ld: region `text' overflowed by 4096 bytes\ncollect2: error: ld returned 1 exit status\n";
    EXPECT_EQ(s2a::toolchain::diagnoseCompile(outcome).problem, Problem::SketchTooBig);
    outcome.err = "/usr/bin/avr-ld: address 0x800a1c of sketch.ino.elf section `.bss' is not within region `data'\n";
    EXPECT_EQ(s2a::toolchain::diagnoseCompile(outcome).problem, Problem::NotEnoughMemory);
}

TEST(Diagnostics, MissingUploadFilesAreNotMissingPorts) {
    CommandOutcome outcome;
    outcome.err = "avrdude: can't open input file /build/sketch.ino.hex: No such file or directory\n";
    EXPECT_NE(s2a::toolchain::diagnoseUpload(outcome, "/dev/ttyACM0").problem, Problem::PortNotFound);
    outcome.err = "avrdude: ser_open(): can't open device \"/dev/ttyACM0\": No such file or directory\n";
    EXPECT_EQ(s2a::toolchain::diagnoseUpload(outcome, "/dev/ttyACM0").problem, Problem::PortNotFound);
}

TEST(ExplainCompilerMessage, ExplainsCommonMistakesOfTypedCode) {
    using s2a::toolchain::CompilerMessage;
    using s2a::toolchain::explainCompilerMessage;
    const auto explain = [](std::string text, s2a::Language language) {
        return explainCompilerMessage(CompilerMessage{"a.ino", 1, 1, "error", std::move(text)}, language);
    };
    EXPECT_EQ(explain("'led' was not declared in this scope", s2a::Language::French).find("\"led\" est inconnu"), 0U);
    EXPECT_NE(explain("\xe2\x80\x98led\xe2\x80\x99 was not declared in this scope", s2a::Language::English).find("\"led\""),
              std::string::npos);
    EXPECT_NE(explain("expected ';' before '}' token", s2a::Language::English).find("\";\" is missing"), std::string::npos);
    EXPECT_NE(explain("expected '}' at end of input", s2a::Language::English).find("\"}\" is missing"), std::string::npos);
    EXPECT_NE(explain("'class Servo' has no member named 'writ'", s2a::Language::English).find("\"writ\""), std::string::npos);
    EXPECT_EQ(explain("invalid conversion from 'int' to 'char*'", s2a::Language::English),
              "invalid conversion from 'int' to 'char*'");
}

} // namespace
