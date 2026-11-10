#include "s2a/script.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

using s2a::Language;
using s2a::live::PinMode;
using s2a::project::ProgramLanguage;
using namespace std::chrono_literals;

/// Pins that log what the program does; the clock advances with delay() (no real waiting).
struct FakePins final : s2a::live::Pins {
    std::vector<std::string> log;
    std::map<int, bool> digitalInputs;
    std::map<int, int> analogInputs;

    void setMode(int pin, PinMode mode) override {
        log.push_back("M" + std::to_string(pin) +
                      (mode == PinMode::Output ? "O" : (mode == PinMode::InputPullup ? "U" : "I")));
    }
    void digitalWrite(int pin, bool high) override { log.push_back("D" + std::to_string(pin) + (high ? "=1" : "=0")); }
    void analogWrite(int pin, int value) override { log.push_back("P" + std::to_string(pin) + "=" + std::to_string(value)); }
    bool digitalRead(int pin) override { return digitalInputs[pin]; }
    int analogRead(int pin) override { return analogInputs[pin]; }
    void tone(int pin, int frequency, int ms) override {
        log.push_back("T" + std::to_string(pin) + "=" + std::to_string(frequency) + "/" + std::to_string(ms));
    }
    void servoWrite(int pin, int angle) override { log.push_back("S" + std::to_string(pin) + "=" + std::to_string(angle)); }
    double distance(int /*trig*/, int /*echo*/) override { return 0; }
    std::optional<std::pair<double, double>> dht(int /*pin*/, int /*model*/) override { return std::nullopt; }
    void lcdLine(int /*row*/, const std::string& /*text*/) override {}
    void lcd16Line(const std::array<int, 6>& /*pins*/, int /*row*/, const std::string& /*text*/) override {}
    void oledLine(int /*row*/, const std::string& /*text*/) override {}
    void pixels(int /*pin*/, int /*count*/, int /*index*/, int /*r*/, int /*g*/, int /*b*/) override {}
    void matrixRow(int /*din*/, int /*clk*/, int /*cs*/, int /*row*/, int /*bits*/) override {}
    void reset() override { log.emplace_back("reset"); }
};

class Script : public ::testing::TestWithParam<ProgramLanguage> {
protected:
    void SetUp() override {
        host.pins = &pins;
        host.board = s2a::BoardDatabase::builtin().find("uno");
        host.print = [this](const std::string& line) { printed.push_back(line); };
        host.reading = [this](const std::string& label, double value) { readings[label] = value; };
        // delay() moves the clock on; after `budget` ms the program is stopped (endless loops).
        host.sleep = [this](std::chrono::milliseconds duration) {
            clock += duration;
            log.push_back("W" + std::to_string(duration.count()));
            if (clock >= budget) {
                stop = true;
            }
            return !stop.load();
        };
        host.now = [this] { return clock; };
        host.stop = &stop;
    }

    std::optional<s2a::script::ScriptError> run(const std::string& code, Language language = Language::English) {
        auto error = s2a::script::run(GetParam(), code, host, language);
        // Waits go in the same log as the pins, in order.
        return error;
    }

    [[nodiscard]] bool python() const { return GetParam() == ProgramLanguage::Python; }

    FakePins pins;
    s2a::script::Host host;
    std::atomic<bool> stop{false};
    std::chrono::milliseconds clock{0};
    std::chrono::milliseconds budget{1'000'000};
    std::vector<std::string> printed;
    std::vector<std::string> log;
    std::map<std::string, double> readings;
};

std::string blink(ProgramLanguage language) {
    if (language == ProgramLanguage::Python) {
        return "pinMode(13, OUTPUT)\n"
               "while True:\n"
               "    digitalWrite(13, HIGH)\n"
               "    delay(1000)\n"
               "    digitalWrite(13, LOW)\n"
               "    delay(1000)\n";
    }
    return "pinMode(13, OUTPUT);\n"
           "while (true) {\n"
           "  digitalWrite(13, HIGH);\n"
           "  delay(1000);\n"
           "  digitalWrite(13, LOW);\n"
           "  delay(1000);\n"
           "}\n";
}

TEST_P(Script, BlinksUntilStopped) {
    budget = 4000ms;
    EXPECT_FALSE(run(blink(GetParam())));
    EXPECT_EQ(pins.log, (std::vector<std::string>{"M13O", "D13=1", "D13=0", "D13=1", "D13=0"}));
    EXPECT_EQ(clock, 4000ms);
}

TEST_P(Script, RunsSetupThenLoopLikeAnArduino) {
    budget = 300ms;
    const std::string code = python() ? "def setup():\n    print('start')\n"
                                        "def loop():\n    digitalWrite(LED_BUILTIN, HIGH)\n    delay(100)\n"
                                      : "function setup() { print('start'); }\n"
                                        "function loop() { digitalWrite(LED_BUILTIN, HIGH); delay(100); }\n";
    EXPECT_FALSE(run(code));
    EXPECT_EQ(printed, std::vector<std::string>{"start"});
    EXPECT_EQ(pins.log, (std::vector<std::string>{"M13O", "D13=1", "D13=1", "D13=1"}));
}

TEST_P(Script, ReadsSensorsAndPrints) {
    pins.analogInputs[14] = 512; // A0 on an Uno
    pins.digitalInputs[2] = true;
    const std::string code = python() ? "print('light', analogRead(A0))\nprint(digitalRead(2) == HIGH)\n"
                                        "import arduino\nprint(arduino.analogRead('A0') + 1)\n"
                                      : "console.log('light', analogRead(A0));\nprint(digitalRead(2) == HIGH);\n"
                                        "print(arduino.analogRead('A0') + 1);\n";
    const auto error = run(code);
    ASSERT_FALSE(error) << error->message << "\n" << error->details;
    EXPECT_EQ(printed, (std::vector<std::string>{"light 512", python() ? "True" : "true", "513"}));
    EXPECT_EQ(readings["A0"], 512);
    EXPECT_EQ(readings["2"], 1);
}

TEST_P(Script, DrivesPwmToneAndServo) {
    const std::string code = python() ? "analogWrite(9, 300)\ntone(8, 440, 200)\nnoTone(8)\nservoWrite(10, 90)\n"
                                      : "analogWrite(9, 300); tone(8, 440, 200); noTone(8); servoWrite(10, 90);\n";
    const auto error = run(code);
    ASSERT_FALSE(error) << error->message;
    EXPECT_EQ(pins.log, (std::vector<std::string>{"M9O", "P9=255", "M8O", "T8=440/200", "T8=0/0", "S10=90"}));
}

TEST_P(Script, ExplainsMistakesWithTheirLine) {
    auto error = run(python() ? "pinMode(13, OUTPUT)\n\ndigitalWrite(led, HIGH)\n" : "pinMode(13, OUTPUT);\n\ndigitalWrite(led, HIGH);\n",
                     Language::French);
    ASSERT_TRUE(error);
    EXPECT_EQ(error->line, 3);
    EXPECT_NE(error->message.find("« led » est inconnu"), std::string::npos) << error->message;
    EXPECT_NE(s2a::script::describe(*error, Language::French).find("Ligne 3"), std::string::npos);

    error = run(python() ? "x = 1\ndigitalWrite(99, HIGH)\n" : "let x = 1;\ndigitalWrite(99, HIGH);\n");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->line, 2);
    EXPECT_EQ(error->message, "Pin 99 does not exist on the Arduino Uno board.");

    error = run(python() ? "analogRead(13)\n" : "analogRead(13);\n");
    ASSERT_TRUE(error);
    EXPECT_NE(error->message.find("analog pin"), std::string::npos) << error->message;
}

TEST_P(Script, ChecksTheSyntaxWithoutRunning) {
    EXPECT_FALSE(s2a::script::check(GetParam(), blink(GetParam()), Language::English));
    const std::string broken = python() ? "x = 1\nif x == 1\n    print(x)\n" : "let x = 1;\nif (x == 1 {\n}\n";
    const auto error = s2a::script::check(GetParam(), broken, Language::English);
    ASSERT_TRUE(error);
    EXPECT_EQ(error->line, 2) << error->details;
    EXPECT_FALSE(error->message.empty());
    EXPECT_TRUE(pins.log.empty());
}

TEST_P(Script, StopsAnEndlessLoopWithoutDelay) {
    host.sleep = nullptr; // real time
    std::thread stopper([this] {
        std::this_thread::sleep_for(200ms);
        stop = true;
    });
    const auto started = std::chrono::steady_clock::now();
    const auto error = run(python() ? "x = 0\nwhile True:\n    x = x + 1\n" : "let x = 0;\nwhile (true) { x++; }\n");
    stopper.join();
    EXPECT_FALSE(error) << error->message;
    EXPECT_LT(std::chrono::steady_clock::now() - started, 5s);
}

TEST_P(Script, StopsALongDelay) {
    host.sleep = nullptr;
    std::thread stopper([this] {
        std::this_thread::sleep_for(100ms);
        stop = true;
    });
    const auto started = std::chrono::steady_clock::now();
    EXPECT_FALSE(run(python() ? "delay(60000)\nprint('too late')\n" : "delay(60000); print('too late');\n"));
    stopper.join();
    EXPECT_TRUE(printed.empty());
    EXPECT_LT(std::chrono::steady_clock::now() - started, 5s);
}

TEST(PythonScript, TimeSleepWaitsLikeDelay) {
    FakePins pins;
    std::atomic<bool> stop{false};
    std::vector<long long> waits;
    s2a::script::Host host;
    host.pins = &pins;
    host.board = s2a::BoardDatabase::builtin().find("uno");
    host.stop = &stop;
    host.sleep = [&waits](std::chrono::milliseconds duration) {
        waits.push_back(duration.count());
        return true;
    };
    const auto error = s2a::script::run(ProgramLanguage::Python, "import time\ntime.sleep(0.5)\n", host, Language::English);
    ASSERT_FALSE(error) << error->message << "\n" << error->details;
    EXPECT_EQ(waits, std::vector<long long>{500});
}

TEST_P(Script, RunsAgainAfterAStop) {
    budget = 1000ms;
    EXPECT_FALSE(run(blink(GetParam())));
    stop = false;
    clock = 0ms;
    pins.log.clear();
    EXPECT_FALSE(run(python() ? "print('again')\n" : "print('again');\n"));
    EXPECT_EQ(printed, std::vector<std::string>{"again"});
}

INSTANTIATE_TEST_SUITE_P(Languages, Script, ::testing::Values(ProgramLanguage::Python, ProgramLanguage::JavaScript),
                         [](const auto& test) { return test.param == ProgramLanguage::Python ? "Python" : "JavaScript"; });

TEST(ScriptLanguages, OnlyPythonAndJavaScriptAreScripts) {
    EXPECT_TRUE(s2a::script::isScript(ProgramLanguage::Python));
    EXPECT_TRUE(s2a::script::isScript(ProgramLanguage::JavaScript));
    EXPECT_FALSE(s2a::script::isScript(ProgramLanguage::Cpp));
    EXPECT_FALSE(s2a::script::isScript(ProgramLanguage::Blocks));
}

} // namespace
