#include "s2a/live.hpp"
#include "s2a/simulator.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace {

using namespace s2a::project;
using s2a::live::LiveLink;
using s2a::live::PinMode;
using s2a::sim::VirtualBoard;
using std::chrono::milliseconds;

const s2a::Board& uno() {
    return *s2a::BoardDatabase::builtin().find("uno");
}

Block block(std::string id, std::string type) {
    Block result;
    result.id = std::move(id);
    result.type = std::move(type);
    return result;
}

template <typename Predicate> bool waitFor(Predicate predicate) {
    for (int i = 0; i < 300; ++i) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    return false;
}

TEST(Simulator, RecognisesItsPort) {
    EXPECT_TRUE(s2a::sim::isSimulated("sim:uno"));
    EXPECT_FALSE(s2a::sim::isSimulated("/dev/ttyACM0"));
    EXPECT_FALSE(s2a::sim::isSimulated("wifi:192.168.4.1"));
    EXPECT_FALSE(s2a::sim::isSimulated("si"));
}

TEST(Simulator, DrivesOutputsLikeTheFirmware) {
    auto board = std::make_shared<VirtualBoard>(uno());
    s2a::sim::Connection connection(board);
    LiveLink link(connection);
    ASSERT_TRUE(link.handshake(milliseconds(500)).ok());

    link.setMode(13, PinMode::Output);
    link.digitalWrite(13, true);
    link.analogWrite(9, 128);
    link.servoWrite(6, 90);
    link.tone(8, 440, 0);
    link.lcdLine(1, "Bonjour");
    link.lcd16Line({12, 11, 10, 9, 8, 7}, 2, "ligne deux");
    link.oledLine(3, "OLED");
    link.pixels(5, 4, 0, 255, 0, 0);
    link.pixels(5, 4, 2, 0, 0, 255);
    link.matrixRow(12, 11, 10, 1, 0x81);
    link.phoneSend("salut");

    auto state = board->state();
    EXPECT_EQ(state.outputs["13"].level, 255);
    EXPECT_FALSE(state.outputs["13"].pwm);
    EXPECT_EQ(state.outputs["9"].level, 128);
    EXPECT_TRUE(state.outputs["9"].pwm);
    EXPECT_EQ(state.modes["13"], "output");
    EXPECT_EQ(state.servos["6"], 90);
    EXPECT_EQ(state.tones["8"], 440);
    EXPECT_EQ(state.lcd[0], "Bonjour");
    EXPECT_EQ(state.lcd16[1], "ligne deux");
    EXPECT_EQ(state.oled[2], "OLED");
    ASSERT_EQ(state.pixels["5"].size(), 4U);
    EXPECT_EQ(state.pixels["5"][0], (std::array<int, 3>{255, 0, 0}));
    EXPECT_EQ(state.pixels["5"][1], (std::array<int, 3>{0, 0, 255}));
    EXPECT_EQ(state.matrices["12"][0], 0x81);
    EXPECT_EQ(state.phone, std::vector<std::string>{"salut"});

    link.digits(4, 5, 42);
    EXPECT_EQ(board->state().digits["4"], "  42");
    link.digits(4, 5, -12345);
    EXPECT_EQ(board->state().digits["4"], "----");
    link.digits(4, 5, std::nullopt);
    EXPECT_EQ(board->state().digits["4"], "");
    link.stepper({8, 9, 10, 11}, 512, 10); // a quarter of a turn
    link.stepper({8, 9, 10, 11}, -1024, 15);
    state = board->state();
    EXPECT_DOUBLE_EQ(state.steppers["8"], -90);
    EXPECT_EQ(state.modes["11"], "output");
    EXPECT_EQ(state.outputs["11"].level, 0); // coils off after the move

    link.tone(8, 0, 0);
    EXPECT_TRUE(board->state().tones.empty());
    link.reset(); // Live mode stopped: everything off
    state = board->state();
    EXPECT_EQ(state.outputs["13"].level, 0);
    EXPECT_TRUE(state.servos.empty());
    EXPECT_EQ(state.lcd[0], "");
    EXPECT_EQ(state.pixels["5"][0], (std::array<int, 3>{0, 0, 0}));
    EXPECT_FALSE(link.takeError().has_value());
}

TEST(Simulator, AShortToneEndsByItself) {
    VirtualBoard board(uno());
    EXPECT_EQ(board.answer("T 8 880 30"), "ok");
    EXPECT_EQ(board.state().tones.at("8"), 880);
    std::this_thread::sleep_for(milliseconds(60));
    EXPECT_TRUE(board.state().tones.empty());
}

TEST(Simulator, ReadsTheInputsTheEditorSets) {
    auto board = std::make_shared<VirtualBoard>(uno());
    s2a::sim::Connection connection(board);
    LiveLink link(connection);
    ASSERT_TRUE(link.handshake(milliseconds(500)).ok());

    // A button between the pin and GND, with the pull-up: HIGH until pressed.
    link.setMode(2, PinMode::InputPullup);
    EXPECT_TRUE(link.digitalRead(2));
    board->press("2", true);
    EXPECT_FALSE(link.digitalRead(2));
    EXPECT_EQ(board->state().pressed.at("2"), true);
    // Without the pull-up the pin reads HIGH while pressed (button wired to 5V).
    link.setMode(3, PinMode::Input);
    EXPECT_FALSE(link.digitalRead(3));
    board->press("3", true);
    EXPECT_TRUE(link.digitalRead(3));

    EXPECT_EQ(link.analogRead(14), s2a::sim::kDefaultAnalog);
    board->setAnalog("A0", 900);
    EXPECT_EQ(link.analogRead(14), 900);
    board->setAnalog("A1", 5000);
    EXPECT_EQ(link.analogRead(15), 1023);

    EXPECT_DOUBLE_EQ(link.distance(7, 8), s2a::sim::kDefaultDistance);
    board->setDistance("7", 12.5);
    EXPECT_DOUBLE_EQ(link.distance(7, 8), 12.5);

    board->setClimate("4", 25.5, 60);
    EXPECT_EQ(link.dht(4, 11), std::optional(std::pair{25.5, 60.0}));

    EXPECT_EQ(link.phoneMessage(), std::nullopt);
    board->phoneSays("Allume\n");
    EXPECT_EQ(link.phoneMessage(), std::optional<std::string>("Allume "));
    EXPECT_EQ(link.phoneMessage(), std::nullopt); // read once

    board->press("99", true); // no such pin: ignored
    EXPECT_EQ(board->state().pressed.count("99"), 0U);
    EXPECT_EQ(board->answer("Z 1"), "! Z 1");
    EXPECT_EQ(board->answer("D x"), "! D x");
}

TEST(Simulator, RunsTheBlocksInLiveMode) {
    // Forever: LED 13 on while the button of pin 2 is pressed.
    Block button = block("button", "sensors_button_pressed");
    button.fields["PIN"] = "2";
    Block on = block("on", "pins_builtin_led");
    on.fields["STATE"] = "ON";
    Block off = block("off", "pins_builtin_led");
    off.fields["STATE"] = "OFF";
    Block choice = block("if", "control_if_else");
    choice.inputs["CONDITION"].block = button;
    choice.statements["SUBSTACK"] = {on};
    choice.statements["SUBSTACK2"] = {off};
    Block forever = block("loop", "control_forever");
    forever.statements["SUBSTACK"] = {choice};
    Project project;
    project.boardId = "uno";
    project.scripts.push_back({0, 0, {block("hat", "events_when_program_starts"), forever}});

    auto board = std::make_shared<VirtualBoard>(uno());
    s2a::live::Session session(
        project, uno(), "sim:uno", s2a::Language::English,
        [board](const std::string&) -> s2a::Result<std::unique_ptr<s2a::serial::LineTransport>> {
            return std::unique_ptr<s2a::serial::LineTransport>(std::make_unique<s2a::sim::Connection>(board));
        },
        s2a::live::Session::Flasher{});
    ASSERT_TRUE(waitFor([&] { return session.snapshot().state == s2a::live::Session::State::Running; }))
        << session.snapshot().message << session.snapshot().details;
    ASSERT_TRUE(waitFor([&] {
        const auto outputs = board->state().outputs;
        return outputs.count("13") != 0 && outputs.at("13").level == 0;
    }));
    board->press("2", true);
    ASSERT_TRUE(waitFor([&] { return board->state().outputs.at("13").level == 255; }));
    board->press("2", false);
    ASSERT_TRUE(waitFor([&] { return board->state().outputs.at("13").level == 0; }));
    session.stop();
}

} // namespace
