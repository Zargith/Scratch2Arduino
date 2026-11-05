#include "s2a/live.hpp"

#include <deque>
#include <gtest/gtest.h>
#include <map>
#include <sstream>
#include <thread>
#include <algorithm>
#include <memory>

namespace {

using namespace s2a::project;
using s2a::BlockCatalog;
using s2a::live::Interpreter;
using s2a::live::PinMode;
using std::chrono::milliseconds;

/// Records what the interpreter does to the board; inputs are set by the test.
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
    double distance(int trig, int echo) override {
        log.push_back("U" + std::to_string(trig) + "/" + std::to_string(echo));
        return distanceCm;
    }
    std::optional<std::pair<double, double>> dht(int pin, int model) override {
        log.push_back("H" + std::to_string(pin) + "/" + std::to_string(model));
        return dhtValues;
    }
    void lcdLine(int row, const std::string& text) override { log.push_back("L" + std::to_string(row) + "=" + text); }
    void oledLine(int row, const std::string& text) override { log.push_back("O" + std::to_string(row) + "=" + text); }
    void pixels(int pin, int count, int index, int red, int green, int blue) override {
        log.push_back("N" + std::to_string(pin) + "/" + std::to_string(count) + "/" + std::to_string(index) + "=" +
                      std::to_string(red) + "," + std::to_string(green) + "," + std::to_string(blue));
    }
    void matrixRow(int din, int clk, int cs, int row, int bits) override {
        log.push_back("G" + std::to_string(din) + "/" + std::to_string(clk) + "/" + std::to_string(cs) + "/" +
                      std::to_string(row) + "=" + std::to_string(bits));
    }
    void reset() override { log.emplace_back("X"); }
    double distanceCm = 400;
    std::optional<std::pair<double, double>> dhtValues;

    std::vector<std::string> take() {
        auto result = std::move(log);
        log.clear();
        return result;
    }
};

Block block(std::string id, std::string type) {
    Block result;
    result.id = std::move(id);
    result.type = std::move(type);
    return result;
}

Block led(std::string id, const char* state) {
    Block result = block(std::move(id), "pins_builtin_led");
    result.fields["STATE"] = state;
    return result;
}

Block wait(std::string id, const char* seconds) {
    Block result = block(std::move(id), "control_wait");
    result.inputs["SECONDS"].value = seconds;
    return result;
}

Project withScript(Stack blocks) {
    Project project;
    project.boardId = "uno";
    project.scripts.push_back({0, 0, std::move(blocks)});
    return project;
}

Project blink(const char* seconds = "1") {
    Block forever = block("loop", "control_forever");
    forever.statements["SUBSTACK"] = {led("on", "ON"), wait("w1", "1"), led("off", "OFF"), wait("w2", seconds)};
    return withScript({block("hat", "events_when_program_starts"), forever});
}

const s2a::Board& uno() {
    return *s2a::BoardDatabase::builtin().find("uno");
}

TEST(Interpreter, BlinksWithoutBlockingBetweenSteps) {
    FakePins pins;
    Interpreter interpreter(blink(), BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"M13O", "D13=1"}));
    EXPECT_EQ(interpreter.runningBlocks(), std::vector<std::string>{"w1"});
    interpreter.step(milliseconds(500));
    EXPECT_TRUE(pins.take().empty()); // still waiting
    interpreter.step(milliseconds(1000));
    EXPECT_EQ(pins.take(), std::vector<std::string>{"D13=0"});
    interpreter.step(milliseconds(2000)); // end of the turn: the loop gives way
    interpreter.step(milliseconds(2010));
    EXPECT_EQ(pins.take(), std::vector<std::string>{"D13=1"});
    EXPECT_FALSE(interpreter.finished());
}

TEST(Interpreter, PlaysATuneAndWaitsForTheEnd) {
    FakePins pins;
    Block note = block("note", "sound_note");
    note.fields["NOTE"] = "440";
    Block play = block("play", "sound_play_tone");
    play.fields["PIN"] = "8";
    play.inputs["FREQUENCY"].block = note;
    play.inputs["SECONDS"].value = "0,5";
    Interpreter interpreter(withScript({block("hat", "events_when_program_starts"), play, led("off", "OFF")}),
                            BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    EXPECT_EQ(pins.take(), std::vector<std::string>{"T8=440/500"});
    interpreter.step(milliseconds(400));
    EXPECT_TRUE(pins.take().empty()); // the sound is still playing
    interpreter.step(milliseconds(500));
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"M13O", "D13=0"}));
}

TEST(Interpreter, DrivesComponents) {
    FakePins pins;
    Block rgb = block("rgb", "lights_rgb_color");
    rgb.fields = {{"RED_PIN", "9"}, {"GREEN_PIN", "10"}, {"BLUE_PIN", "11"}, {"COLOR", "YELLOW"}};
    Block servo = block("servo", "motors_servo_write");
    servo.fields["PIN"] = "3";
    servo.inputs["ANGLE"].value = "250";
    Block motor = block("motor", "motors_dc_run");
    motor.fields = {{"EN_PIN", "6"}, {"IN1_PIN", "7"}, {"IN2_PIN", "4"}, {"DIRECTION", "BACKWARD"}};
    motor.inputs["SPEED"].value = "50";
    Block stop = block("stop", "motors_dc_stop");
    stop.fields = {{"EN_PIN", "6"}, {"IN1_PIN", "7"}, {"IN2_PIN", "4"}};
    Interpreter interpreter(withScript({block("hat", "events_when_program_starts"), rgb, servo, motor, stop}),
                            BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"M9O", "P9=255", "M10O", "P10=160", "M11O", "P11=0", "S3=180",
                                                     "M6O", "M7O", "M4O", "D7=0", "D4=1", "P6=127", "D7=0", "D4=0",
                                                     "P6=0"}));
    EXPECT_TRUE(interpreter.finished());
}

TEST(Interpreter, ReadsSensors) {
    FakePins pins;
    pins.distanceCm = 12.5;
    pins.analogInputs[14] = 1023;
    pins.digitalInputs[2] = false; // pressed: wired to GND
    const auto setter = [](const char* id, const char* variable, Block value) {
        Block set = block(id, "variables_set");
        set.fields["VARIABLE"] = variable;
        set.inputs["VALUE"].block = std::move(value);
        return set;
    };
    Block distance = block("distance", "sensors_distance");
    distance.fields = {{"TRIG_PIN", "12"}, {"ECHO_PIN", "11"}};
    Block knob = block("knob", "sensors_potentiometer");
    knob.fields["PIN"] = "A0";
    Block button = block("button", "sensors_button_pressed");
    button.fields["PIN"] = "2";
    Project project = withScript({block("hat", "events_when_program_starts"), setter("s1", "d", distance),
                                  setter("s2", "k", knob), setter("s3", "b", button)});
    project.variables = {{"d", "d", 0}, {"k", "k", 0}, {"b", "b", 0}};
    Interpreter interpreter(project, BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"U12/11", "M14I", "M2U"}));
    EXPECT_DOUBLE_EQ(interpreter.variables().at("d"), 12.5);
    EXPECT_DOUBLE_EQ(interpreter.variables().at("k"), 100);
    EXPECT_DOUBLE_EQ(interpreter.variables().at("b"), 1);
    EXPECT_DOUBLE_EQ(interpreter.readings().at("11"), 12.5);
    EXPECT_DOUBLE_EQ(interpreter.readings().at("A0"), 100);
}

TEST(Interpreter, DrivesScreensStripsAndMatrices) {
    FakePins pins;
    Block knob = block("knob", "sensors_potentiometer");
    knob.fields["PIN"] = "A0";
    Block lcd = block("lcd", "display_lcd_print");
    lcd.fields["LINE"] = "2";
    lcd.inputs["TEXT"].block = knob;
    Block oled = block("oled", "display_oled_print");
    oled.fields["LINE"] = "8";
    oled.inputs["TEXT"].value = "Été";
    Block fill = block("fill", "lights_neopixel_fill");
    fill.fields = {{"PIN", "5"}, {"COLOR", "BLUE"}};
    fill.inputs["COUNT"].value = "12";
    Block one = block("one", "lights_neopixel_set");
    one.fields = {{"PIN", "5"}, {"COLOR", "GREEN"}};
    one.inputs["COUNT"].value = "12";
    one.inputs["INDEX"].value = "13"; // outside the strip: nothing happens
    Block show = block("show", "lights_matrix_show");
    show.fields["IMAGE"] = "CROSS";
    Block dot = block("dot", "lights_matrix_dot");
    dot.fields["STATE"] = "OFF";
    dot.inputs["X"].value = "1";
    dot.inputs["Y"].value = "1";
    pins.analogInputs[14] = 1023;
    Interpreter interpreter(withScript({block("hat", "events_when_program_starts"), lcd, oled,
                                        block("clear", "display_lcd_clear"), fill, one, show, dot}),
                            BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    const std::vector<std::string> expected{"M14I", "L2=100", "O8=Été", "L0=", "N5/12/0=0,0,255", "M12O", "M11O",
                                            "M10O", "G12/11/10/1=129", "G12/11/10/2=66", "G12/11/10/3=36",
                                            "G12/11/10/4=24", "G12/11/10/5=24", "G12/11/10/6=36", "G12/11/10/7=66",
                                            "G12/11/10/8=129", "G12/11/10/1=1"};
    EXPECT_EQ(pins.take(), expected);
}

TEST(Interpreter, KeepsTheLastGoodDhtReading) {
    FakePins pins;
    Block temperature = block("t", "sensors_temperature");
    temperature.fields = {{"MODEL", "DHT22"}, {"PIN", "3"}};
    Block set = block("set", "variables_set");
    set.fields["VARIABLE"] = "t";
    set.inputs["VALUE"].block = temperature;
    Block forever = block("loop", "control_forever");
    forever.statements["SUBSTACK"] = {set};
    Project project = withScript({block("hat", "events_when_program_starts"), forever});
    project.variables = {{"t", "t", 0}};
    pins.dhtValues = std::pair{21.5, 40.0};
    Interpreter interpreter(project, BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    EXPECT_DOUBLE_EQ(interpreter.variables().at("t"), 21.5);
    pins.dhtValues.reset(); // no answer this time
    interpreter.step(milliseconds(10));
    EXPECT_DOUBLE_EQ(interpreter.variables().at("t"), 21.5);
    EXPECT_DOUBLE_EQ(interpreter.readings().at("3 (%)"), 40);
    EXPECT_EQ(pins.take().front(), "H3/22");
}

TEST(Interpreter, RepeatsAndFinishes) {
    FakePins pins;
    Block repeat = block("repeat", "control_repeat");
    repeat.inputs["TIMES"].value = "3";
    repeat.statements["SUBSTACK"] = {led("on", "ON")};
    Interpreter interpreter(withScript({block("hat", "events_when_program_starts"), repeat, led("off", "OFF")}),
                            BlockCatalog::builtin(), uno(), pins);
    for (int i = 0; i < 10 && !interpreter.finished(); ++i) {
        interpreter.step(milliseconds(i));
    }
    EXPECT_TRUE(interpreter.finished());
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"M13O", "D13=1", "D13=1", "D13=1", "D13=0"}));
}

TEST(Interpreter, ReadsInputsForConditions) {
    FakePins pins;
    Block read = block("read", "pins_digital_read");
    read.fields["PIN"] = "2";
    Block write = block("write", "pins_digital_write");
    write.fields = {{"PIN", "7"}, {"STATE", "HIGH"}};
    Block until = block("until", "control_wait_until");
    until.inputs["CONDITION"].block = read;
    Interpreter interpreter(withScript({block("hat", "events_when_program_starts"), until, write}), BlockCatalog::builtin(),
                            uno(), pins);
    interpreter.step(milliseconds(0));
    interpreter.step(milliseconds(10));
    EXPECT_EQ(pins.take(), std::vector<std::string>{"M2I"}); // waiting for the button
    EXPECT_EQ(interpreter.runningBlocks(), std::vector<std::string>{"until"});
    pins.digitalInputs[2] = true;
    interpreter.step(milliseconds(20));
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"M7O", "D7=1"}));
    EXPECT_TRUE(interpreter.finished());
}

TEST(Interpreter, ReadsAButtonWithPullupAsLow) {
    FakePins pins;
    Block mode = block("mode", "pins_set_mode");
    mode.fields = {{"PIN", "2"}, {"MODE", "INPUT_PULLUP"}};
    Block read = block("read", "pins_digital_read");
    read.fields = {{"PIN", "2"}, {"STATE", "LOW"}};
    Block write = block("write", "pins_digital_write");
    write.fields = {{"PIN", "7"}, {"STATE", "HIGH"}};
    Block until = block("until", "control_wait_until");
    until.inputs["CONDITION"].block = read;
    pins.digitalInputs[2] = true; // released: the pull-up keeps the pin HIGH
    Interpreter interpreter(withScript({block("hat", "events_when_program_starts"), mode, until, write}),
                            BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    interpreter.step(milliseconds(10));
    EXPECT_EQ(pins.take(), std::vector<std::string>{"M2U"}); // reading keeps the pull-up
    pins.digitalInputs[2] = false; // pressed
    interpreter.step(milliseconds(20));
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"M7O", "D7=1"}));
    EXPECT_TRUE(interpreter.finished());
}

TEST(Interpreter, SetsAPlainInputAfterAPullup) {
    FakePins pins;
    Block pullup = block("pullup", "pins_set_mode");
    pullup.fields = {{"PIN", "3"}, {"MODE", "INPUT_PULLUP"}};
    Block plain = block("plain", "pins_set_mode");
    plain.fields = {{"PIN", "3"}, {"MODE", "INPUT"}};
    Interpreter interpreter(withScript({block("hat", "events_when_program_starts"), pullup, plain}),
                            BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"M3U", "M3I"}));
}

TEST(Interpreter, WritesLinesAndRecordsReadings) {
    FakePins pins;
    pins.analogInputs[14] = 512; // A0 on an Uno
    Block text = block("text", "serial_print");
    text.inputs["VALUE"].value = "mesure";
    Block analog = block("analog", "pins_analog_read");
    analog.fields["PIN"] = "A0";
    Block value = block("value", "serial_print");
    value.inputs["VALUE"].block = analog;
    Block divide = block("divide", "operators_divide");
    divide.inputs["NUM1"].value = "1";
    divide.inputs["NUM2"].value = "3";
    Block third = block("third", "serial_print");
    third.inputs["VALUE"].block = divide;
    Interpreter interpreter(withScript({block("hat", "events_when_program_starts"), text, value, third}),
                            BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    EXPECT_EQ(interpreter.takeOutput(), (std::vector<std::string>{"mesure", "512", "0.33"}));
    EXPECT_TRUE(interpreter.takeOutput().empty());
    EXPECT_EQ(interpreter.readings(), (std::map<std::string, double>{{"A0", 512}}));
}

TEST(Interpreter, ComputesWithVariables) {
    FakePins pins;
    Project project;
    project.boardId = "uno";
    project.variables.push_back({"v", "level", 10});
    Block get = block("get", "variables_get");
    get.fields["VARIABLE"] = "v";
    Block analog = block("analog", "pins_analog_read");
    analog.fields["PIN"] = "A0";
    Block map = block("map", "operators_map");
    map.inputs["VALUE"].block = analog;
    map.inputs["FROM_LOW"].value = "0";
    map.inputs["FROM_HIGH"].value = "1023";
    map.inputs["TO_LOW"].value = "0";
    map.inputs["TO_HIGH"].value = "255";
    Block change = block("change", "variables_change");
    change.fields["VARIABLE"] = "v";
    change.inputs["VALUE"].block = map;
    Block pwm = block("pwm", "pins_analog_write");
    pwm.fields["PIN"] = "9";
    pwm.inputs["VALUE"].block = get;
    project.scripts.push_back({0, 0, {block("hat", "events_when_program_starts"), change, pwm}});
    pins.analogInputs[14] = 1023; // A0 is pin 14 on an Uno
    Interpreter interpreter(project, BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    EXPECT_DOUBLE_EQ(interpreter.variables().at("v"), 265);
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"M14I", "M9O", "P9=255"})); // clamped to 255
}

TEST(Interpreter, RunsScriptsSideBySide) {
    FakePins pins;
    Project project = blink();
    Block forever = block("loop2", "control_forever");
    Block write = block("write", "pins_digital_write");
    write.fields = {{"PIN", "7"}, {"STATE", "HIGH"}};
    forever.statements["SUBSTACK"] = {write, wait("w3", "0.5")};
    project.scripts.push_back({300, 0, {block("hat2", "events_when_program_starts"), forever}});
    Interpreter interpreter(project, BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    EXPECT_EQ(pins.take(), (std::vector<std::string>{"M13O", "D13=1", "M7O", "D7=1"}));
    EXPECT_EQ(interpreter.runningBlocks(), (std::vector<std::string>{"w1", "w3"}));
}

TEST(Interpreter, ChangesTakeEffectWithoutRestarting) {
    FakePins pins;
    Project project = blink("1");
    project.variables.push_back({"v", "count", 0});
    Interpreter interpreter(project, BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0)); // LED on, waiting in w1
    (void)pins.take();

    // The child changes the second wait to 0.2 s while the board runs.
    Project edited = blink("0.2");
    edited.variables = project.variables;
    interpreter.update(edited);
    EXPECT_EQ(interpreter.runningBlocks(), std::vector<std::string>{"w1"}); // same place
    interpreter.step(milliseconds(1000)); // the wait started before the change ends as planned
    EXPECT_EQ(pins.take(), std::vector<std::string>{"D13=0"});
    interpreter.step(milliseconds(1200)); // the new duration of w2 is used
    interpreter.step(milliseconds(1210));
    EXPECT_EQ(pins.take(), std::vector<std::string>{"D13=1"});
}

TEST(Interpreter, RestartsAScriptWhoseBlocksDisappeared) {
    FakePins pins;
    Interpreter interpreter(blink(), BlockCatalog::builtin(), uno(), pins);
    interpreter.step(milliseconds(0));
    (void)pins.take();
    Project other = withScript({block("hat", "events_when_program_starts"), led("solo", "OFF")});
    interpreter.update(other);
    interpreter.step(milliseconds(10));
    EXPECT_EQ(pins.take(), std::vector<std::string>{"D13=0"});
    EXPECT_TRUE(interpreter.finished());
}

/// Answers like the S2A live firmware.
struct FakeFirmware final : s2a::serial::LineTransport {
    std::deque<std::string> pending{std::string(s2a::live::kHello)};
    std::vector<std::string> received;
    bool silent = false;

    s2a::Status writeLine(std::string_view line) override {
        received.emplace_back(line);
        if (silent) {
            return s2a::success();
        }
        std::istringstream words{std::string(line)};
        std::string command;
        int pin = 0;
        words >> command >> pin;
        if (command == "?") {
            pending.emplace_back(s2a::live::kHello);
        } else if (command == "R") {
            pending.push_back("r " + std::to_string(pin) + " 1");
        } else if (command == "A") {
            pending.push_back("a " + std::to_string(pin) + " 512");
        } else if (command == "H") {
            pending.push_back("h " + std::to_string(pin) + " 215 -9999");
        } else if (command == "U") {
            pending.push_back("u " + std::to_string(pin) + " 1234");
        } else {
            pending.emplace_back("ok");
        }
        return s2a::success();
    }

    s2a::Result<std::optional<std::string>> readLine(milliseconds /*timeout*/) override {
        if (pending.empty()) {
            return std::optional<std::string>();
        }
        std::string line = pending.front();
        pending.pop_front();
        return std::optional<std::string>(line);
    }
};

TEST(LiveLink, SpeaksTheFirmwareProtocol) {
    FakeFirmware firmware;
    s2a::live::LiveLink link(firmware);
    ASSERT_TRUE(link.handshake().ok());
    link.setMode(13, PinMode::Output);
    link.digitalWrite(13, true);
    link.analogWrite(9, 300);
    EXPECT_TRUE(link.digitalRead(2));
    EXPECT_EQ(link.analogRead(14), 512);
    link.reset();
    EXPECT_EQ(firmware.received, (std::vector<std::string>{"?", "M 13 O", "D 13 1", "P 9 255", "R 2", "A 14", "X"}));
    EXPECT_FALSE(link.takeError().has_value());
}

/// A board reset by the opening of the port: an old greeting is still in the port, and the
/// bootloader swallows what is sent until the firmware greets again.
struct RestartingFirmware final : s2a::serial::LineTransport {
    std::deque<std::string> pending{std::string(s2a::live::kHello)}; // left over from before the reset
    std::vector<std::string> received;
    int swallowed = 0;
    int silentReads = 3; ///< Reads before the firmware starts and greets.

    s2a::Status writeLine(std::string_view line) override {
        received.emplace_back(line);
        if (silentReads > 0) {
            ++swallowed; // the bootloader runs
        } else {
            pending.emplace_back(line == "?" ? std::string(s2a::live::kHello) : std::string("ok"));
        }
        return s2a::success();
    }

    s2a::Result<std::optional<std::string>> readLine(milliseconds /*timeout*/) override {
        if (!pending.empty()) {
            std::string line = pending.front();
            pending.pop_front();
            return std::optional<std::string>(line);
        }
        if (silentReads > 0 && --silentReads == 0) {
            return std::optional<std::string>(std::string(s2a::live::kHello)); // the firmware starts
        }
        return std::optional<std::string>();
    }
};

TEST(LiveLink, WaitsForABoardThatRestartsWhenThePortOpens) {
    RestartingFirmware firmware;
    s2a::live::LiveLink link(firmware);
    ASSERT_TRUE(link.handshake().ok());
    EXPECT_GE(firmware.swallowed, 1); // the old greeting was not trusted
    link.setMode(6, PinMode::Input);
    EXPECT_FALSE(link.takeError().has_value()) << "the first command must reach the started firmware";
    EXPECT_EQ(firmware.received.back(), "M 6 I");
}

TEST(LiveLink, DrivesComponents) {
    FakeFirmware firmware;
    s2a::live::LiveLink link(firmware);
    ASSERT_TRUE(link.handshake().ok());
    link.tone(8, 440, 500);
    link.tone(8, 0, 0);
    link.servoWrite(9, 200);
    EXPECT_DOUBLE_EQ(link.distance(12, 11), 123.4);
    EXPECT_FALSE(link.dht(3, 11).has_value()); // no humidity: the sensor did not answer
    link.lcdLine(1, "Température : 21 °C");
    link.oledLine(0, "");
    link.pixels(5, 8, 0, 255, 300, 0);
    link.matrixRow(12, 11, 10, 3, 0x1FF);
    EXPECT_EQ(firmware.received,
              (std::vector<std::string>{"?", "T 8 440 500", "T 8 0 0", "S 9 180", "U 12 11", "H 3 11",
                                        "L 1 Temperature : 21", "O 0 ", "N 5 8 0 255 255 0", "G 12 11 10 3 255"}));
    EXPECT_FALSE(link.takeError().has_value());
}

TEST(LiveLink, ReportsABoardThatDoesNotAnswer) {
    FakeFirmware firmware;
    firmware.pending.clear();
    firmware.silent = true;
    s2a::live::LiveLink link(firmware);
    EXPECT_FALSE(link.handshake(milliseconds(50)).ok());
    link.digitalWrite(13, true);
    const auto error = link.takeError();
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->code, s2a::ErrorCode::BoardNotResponding);
}

/// A FakeFirmware owned by the session, whose received lines stay readable by the test.
struct SharedFirmware final : s2a::serial::LineTransport {
    std::shared_ptr<FakeFirmware> firmware;
    explicit SharedFirmware(std::shared_ptr<FakeFirmware> shared) : firmware(std::move(shared)) {}
    s2a::Status writeLine(std::string_view line) override { return firmware->writeLine(line); }
    s2a::Result<std::optional<std::string>> readLine(milliseconds timeout) override { return firmware->readLine(timeout); }
};

template <typename Predicate> bool waitFor(Predicate predicate, int tenthsOfSeconds = 30) {
    for (int i = 0; i < tenthsOfSeconds * 10; ++i) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    return false;
}

using s2a::live::Session;

TEST(Session, RunsUpdatesAndStops) {
    auto firmware = std::make_shared<FakeFirmware>();
    int flashes = 0;
    {
        Session session(
            blink(), uno(), "COM1", s2a::Language::English,
            [firmware](const std::string&) -> s2a::Result<std::unique_ptr<s2a::serial::LineTransport>> {
                return std::unique_ptr<s2a::serial::LineTransport>(std::make_unique<SharedFirmware>(firmware));
            },
            [&flashes](std::string&) {
                ++flashes;
                return s2a::success();
            });
        ASSERT_TRUE(waitFor([&] { return session.snapshot().state == Session::State::Running; }));
        ASSERT_TRUE(waitFor([&] { return !session.snapshot().runningBlocks.empty(); }));
        EXPECT_EQ(session.snapshot().runningBlocks, std::vector<std::string>{"w1"});
        session.update(withScript({block("hat", "events_when_program_starts"), led("solo", "OFF")}));
        ASSERT_TRUE(waitFor([&] { return session.snapshot().state == Session::State::Finished; }));
        EXPECT_TRUE(session.active() == false);
    }
    EXPECT_EQ(flashes, 0);
    const auto& received = firmware->received;
    EXPECT_NE(std::find(received.begin(), received.end(), "D 13 0"), received.end());
    EXPECT_EQ(received.back(), "X"); // outputs switched off when Live mode ends
}

TEST(Session, UploadsTheFirmwareWhenTheBoardDoesNotAnswer) {
    auto silent = std::make_shared<FakeFirmware>();
    silent->pending.clear();
    silent->silent = true;
    auto ready = std::make_shared<FakeFirmware>();
    int opened = 0;
    int flashes = 0;
    Session session(
        blink(), uno(), "/dev/ttyACM0", s2a::Language::French,
        [&](const std::string&) -> s2a::Result<std::unique_ptr<s2a::serial::LineTransport>> {
            return std::unique_ptr<s2a::serial::LineTransport>(
                std::make_unique<SharedFirmware>(opened++ == 0 ? silent : ready));
        },
        [&flashes](std::string&) {
            ++flashes;
            return s2a::success();
        });
    // The silent board is probed for a few seconds before the firmware is uploaded.
    ASSERT_TRUE(waitFor([&] { return session.snapshot().state == Session::State::Running; }, 80));
    EXPECT_EQ(flashes, 1);
    EXPECT_EQ(opened, 2);
}

TEST(Session, RefusesBlocksWithMistakes) {
    Block condition = block("if", "control_if");
    bool opened = false;
    Session session(
        withScript({block("hat", "events_when_program_starts"), condition}), uno(), "COM1", s2a::Language::English,
        [&opened](const std::string&) -> s2a::Result<std::unique_ptr<s2a::serial::LineTransport>> {
            opened = true;
            return s2a::Error{s2a::ErrorCode::PortFailed, "unused"};
        },
        [](std::string&) { return s2a::success(); });
    ASSERT_TRUE(waitFor([&] { return session.snapshot().state == Session::State::Failed; }));
    EXPECT_EQ(session.snapshot().problemBlocks, std::vector<std::string>{"if"});
    EXPECT_FALSE(opened);
}

TEST(LiveLink, EmbedsTheFirmware) {
    const auto source = s2a::live::firmwareSource();
    EXPECT_NE(source.find(std::string("\"") + std::string(s2a::live::kHello) + "\""), std::string_view::npos);
    EXPECT_NE(source.find("void loop()"), std::string_view::npos);
}

} // namespace
