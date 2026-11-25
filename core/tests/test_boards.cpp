#include "s2a/boards.hpp"

#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

namespace {

using s2a::Board;
using s2a::BoardDatabase;
using s2a::PinCapability;

std::vector<int> numbers(const std::vector<const s2a::Pin*>& pins) {
    std::vector<int> result;
    for (const auto* pin : pins) {
        result.push_back(pin->number);
    }
    return result;
}

TEST(BuiltinBoards, LoadsEveryBoard) {
    const auto& database = BoardDatabase::builtin();
    ASSERT_EQ(database.boards().size(), 12U);
    for (const char* id : {"uno", "nano", "mega", "leonardo", "micro", "nano_every", "uno_r4_minima", "uno_r4_wifi",
                           "pico", "pico_w", "esp32", "esp8266"}) {
        EXPECT_NE(database.find(id), nullptr) << id;
    }
    EXPECT_EQ(database.find("due"), nullptr);
    EXPECT_NE(database.find(BoardDatabase::kDefaultBoardId), nullptr);
}

TEST(BuiltinBoards, LeonardoAndMicroNumberTheirAnalogPinsFrom18) {
    const auto& database = BoardDatabase::builtin();
    for (const char* id : {"leonardo", "micro"}) {
        const Board& board = *database.find(id);
        EXPECT_EQ(board.mcu, "ATmega32U4");
        ASSERT_NE(board.findPin("A0"), nullptr);
        EXPECT_EQ(board.findPin("A0")->number, 18);
        EXPECT_TRUE(board.findPin("13")->has(PinCapability::Pwm));
        EXPECT_TRUE(board.findPin("7")->has(PinCapability::Interrupt));
        // I2C on 2 and 3, not on A4 / A5 as on the Uno.
        ASSERT_EQ(board.i2c.size(), 2U);
    }
    EXPECT_NE(database.find("micro")->findPin("16"), nullptr);    // MOSI, on the header
    EXPECT_EQ(database.find("leonardo")->findPin("16"), nullptr); // only on the ICSP header
}

TEST(BuiltinBoards, ModuleReceivesOnlyWhereSoftwareSerialCan) {
    const auto& database = BoardDatabase::builtin();
    const Board& leonardo = *database.find("leonardo");
    EXPECT_EQ(bluetoothModuleProblem(leonardo, 8, 9), "");
    EXPECT_EQ(bluetoothModuleProblem(leonardo, 2, 9), "pin_receive");
    EXPECT_EQ(bluetoothModuleProblem(*database.find("uno"), 2, 3), ""); // every Uno pin can receive
    const Board& mega = *database.find("mega");
    EXPECT_EQ(moduleSerialPort(mega, 19, 18), 1);
    EXPECT_EQ(moduleSerialPort(mega, 15, 14), 3);
    EXPECT_EQ(bluetoothModuleProblem(mega, 17, 16), "");          // Serial2
    EXPECT_EQ(bluetoothModuleProblem(mega, 62, 9), "");           // A8: SoftwareSerial receives there
    EXPECT_EQ(bluetoothModuleProblem(mega, 2, 3), "pin_receive");
    EXPECT_EQ(moduleSerialPort(leonardo, 19, 18), 0);
    EXPECT_EQ(bluetoothModuleProblem(*database.find("nano_every"), 8, 9), "no_module");
}

TEST(BuiltinBoards, PicoUsesGpioNamesAndItsOnBoardLed) {
    const auto& database = BoardDatabase::builtin();
    const Board& pico = *database.find("pico");
    EXPECT_DOUBLE_EQ(pico.voltage, 3.3);
    ASSERT_NE(pico.findPin("GP26"), nullptr);
    EXPECT_EQ(pico.findPin("GP26")->number, 26);
    EXPECT_TRUE(pico.findPin("GP26")->has(PinCapability::Analog));
    EXPECT_FALSE(pico.findPin("GP2")->has(PinCapability::Analog));
    EXPECT_TRUE(pico.findPin("GP2")->has(PinCapability::Pwm));
    EXPECT_EQ(pico.findPin("GP23"), nullptr); // not on the header
    EXPECT_EQ(pico.builtinLed, 25);
    EXPECT_EQ(database.find("pico_w")->builtinLed, 64); // driven through the Wi-Fi chip
}

TEST(BuiltinBoards, NewArduinoBoardsKeepTheirFormFactor) {
    const auto& database = BoardDatabase::builtin();
    for (const char* id : {"uno_r4_minima", "uno_r4_wifi"}) {
        const Board& board = *database.find(id);
        EXPECT_EQ(board.architecture, "renesas_uno");
        EXPECT_EQ(board.findPin("A0")->number, 14);
        EXPECT_EQ(board.drawing.width, database.find("uno")->drawing.width);
    }
    const Board& every = *database.find("nano_every");
    EXPECT_EQ(every.architecture, "megaavr");
    EXPECT_TRUE(every.findPin("A7")->has(PinCapability::Digital)); // unlike the Nano
    EXPECT_FALSE(database.find("nano")->findPin("A7")->has(PinCapability::Digital));
}

// The embedded descriptions must parse: builtin() silently returns an empty database otherwise.
TEST(BuiltinBoards, EmbeddedDescriptionsAreValid) {
    EXPECT_FALSE(BoardDatabase::builtin().boards().empty());
}

TEST(Uno, PinsMatchTheHardware) {
    const auto* uno = BoardDatabase::builtin().find("uno");
    ASSERT_NE(uno, nullptr);
    EXPECT_EQ(uno->fqbn, "arduino:avr:uno");
    EXPECT_EQ(uno->pins.size(), 20U);
    EXPECT_EQ(uno->builtinLed, 13);
    EXPECT_EQ(numbers(uno->pinsWith(PinCapability::Pwm)), (std::vector<int>{3, 5, 6, 9, 10, 11}));
    EXPECT_EQ(numbers(uno->pinsWith(PinCapability::Interrupt)), (std::vector<int>{2, 3}));
    EXPECT_EQ(numbers(uno->pinsWith(PinCapability::Analog)), (std::vector<int>{14, 15, 16, 17, 18, 19}));
    EXPECT_EQ(uno->pinsWith(PinCapability::Digital).size(), 20U);
}

TEST(Esp32, ListsItsGpioPins) {
    const auto* esp32 = BoardDatabase::builtin().find("esp32");
    ASSERT_NE(esp32, nullptr);
    EXPECT_EQ(esp32->architecture, "esp32");
    EXPECT_EQ(esp32->builtinLed, 2);
    // Flash pins (6-11), serial (1, 3) and boot pins (0, 12) are left out.
    for (const int hidden : {0, 1, 3, 6, 7, 8, 9, 10, 11, 12}) {
        EXPECT_EQ(esp32->findPin(hidden), nullptr) << hidden;
    }
    // Analog inputs usable while Wi-Fi is on (ADC1); 34-39 are inputs only.
    EXPECT_EQ(numbers(esp32->pinsWith(PinCapability::Analog)), (std::vector<int>{32, 33, 34, 35, 36, 39}));
    ASSERT_NE(esp32->findPin("34"), nullptr);
    EXPECT_FALSE(esp32->findPin("34")->has(PinCapability::Digital));
    EXPECT_TRUE(esp32->findPin("D25")->has(PinCapability::Pwm));
    EXPECT_EQ(esp32->pinsWith(PinCapability::Digital).size(), 18U);
}

TEST(Uno, FindsPinsByLabelOrNumber) {
    const auto* uno = BoardDatabase::builtin().find("uno");
    ASSERT_NE(uno, nullptr);
    ASSERT_NE(uno->findPin("A0"), nullptr);
    EXPECT_EQ(uno->findPin("A0")->number, 14);
    EXPECT_EQ(uno->findPin("a0")->number, 14);
    EXPECT_EQ(uno->findPin("14")->label, "A0");
    EXPECT_EQ(uno->findPin("D13")->number, 13);
    EXPECT_EQ(uno->findPin("13")->number, 13);
    EXPECT_EQ(uno->findPin("D14"), nullptr); // 14 is A0, not a "D" pin
    EXPECT_EQ(uno->findPin("A6"), nullptr);
    EXPECT_EQ(uno->findPin("20"), nullptr);
    EXPECT_EQ(uno->findPin(""), nullptr);
    EXPECT_EQ(uno->findPin("-1"), nullptr);
}

TEST(Nano, A6AndA7AreAnalogInputsOnly) {
    const auto* nano = BoardDatabase::builtin().find("nano");
    ASSERT_NE(nano, nullptr);
    const auto* a6 = nano->findPin("A6");
    ASSERT_NE(a6, nullptr);
    EXPECT_TRUE(a6->has(PinCapability::Analog));
    EXPECT_FALSE(a6->has(PinCapability::Digital));
    EXPECT_TRUE(nano->findPin("A5")->has(PinCapability::Digital));
}

TEST(Mega, HasSeventyPinsAndFifteenPwm) {
    const auto* mega = BoardDatabase::builtin().find("mega");
    ASSERT_NE(mega, nullptr);
    EXPECT_EQ(mega->pins.size(), 70U);
    EXPECT_EQ(mega->pinsWith(PinCapability::Pwm).size(), 15U);
    EXPECT_EQ(mega->findPin("A15")->number, 69);
    ASSERT_EQ(mega->i2c.size(), 2U);
}

TEST(BoardDatabase, MatchesUsbIds) {
    const auto& database = BoardDatabase::builtin();
    const auto uno = database.matchUsb({0x2341, 0x0043});
    ASSERT_EQ(uno.size(), 1U);
    EXPECT_EQ(uno[0]->id, "uno");
    EXPECT_TRUE(database.matchUsb({0xFFFF, 0xFFFF}).empty());
}

constexpr std::string_view kMinimalBoard = R"({
  "id": "test", "name": "Test", "fqbn": "a:b:c", "platform": "a:b", "mcu": "X",
  "voltage": 3.3, "flashBytes": 1, "ramBytes": 1, "builtinLed": "1",
  "digitalPins": 2, "analogPins": {"count": 1, "firstNumber": 2},
  "pwmPins": ["1"], "interruptPins": []
})";

TEST(BoardDatabase, ParsesAMinimalBoard) {
    const auto result = BoardDatabase::fromJson({kMinimalBoard});
    ASSERT_TRUE(result.ok()) << result.error().detail;
    const auto& board = result.value().boards().front();
    EXPECT_EQ(board.pins.size(), 3U);
    EXPECT_TRUE(board.i2c.empty());
    EXPECT_TRUE(board.usbIds.empty());
}

TEST(BoardDatabase, RejectsInvalidDescriptions) {
    const auto expectInvalid = [](std::string document, std::string_view from, std::string_view to) {
        document.replace(document.find(from), from.size(), to);
        const auto result = BoardDatabase::fromJson({document});
        EXPECT_FALSE(result.ok()) << "accepted: " << to;
    };
    const std::string base{kMinimalBoard};
    expectInvalid(base, R"("pwmPins": ["1"])", R"("pwmPins": ["9"])");          // unknown pin
    expectInvalid(base, R"("builtinLed": "1")", R"("builtinLed": "A9")");       // unknown LED pin
    expectInvalid(base, R"("fqbn": "a:b:c")", R"("fqbn": "abc")");             // malformed fqbn
    expectInvalid(base, R"("id": "test")", R"("id": "Bad Id")");               // invalid id
    expectInvalid(base, R"("firstNumber": 2)", R"("firstNumber": 1)");         // pin number reused
    expectInvalid(base, R"("mcu": "X")", R"("mcu": 3)");                       // wrong type
    expectInvalid(base, R"("interruptPins": [])", R"("interruptPins": [], "usb": ["12"])");
    EXPECT_FALSE(BoardDatabase::fromJson({"{"}).ok());
    EXPECT_FALSE(BoardDatabase::fromJson({kMinimalBoard, kMinimalBoard}).ok()); // duplicate id
}

} // namespace

namespace {

TEST(BuiltinBoards, NamesTheNodeMcuPinsAsPrintedOnIt) {
    const auto* board = s2a::BoardDatabase::builtin().find("esp8266");
    ASSERT_NE(board, nullptr);
    const auto* d1 = board->findPin("D1");
    ASSERT_NE(d1, nullptr);
    EXPECT_EQ(d1->number, 5); // GPIO 5
    EXPECT_EQ(d1->label, "D1");
    EXPECT_EQ(board->findPin("d1"), d1);
    EXPECT_EQ(board->findPin("5"), d1);
    EXPECT_EQ(board->findPin("1"), nullptr); // GPIO 1 is the serial port
    const auto* a0 = board->findPin("A0");
    ASSERT_NE(a0, nullptr);
    EXPECT_EQ(a0->number, 17);
    EXPECT_TRUE(a0->has(s2a::PinCapability::Analog));
    EXPECT_FALSE(a0->has(s2a::PinCapability::Digital));
    EXPECT_FALSE(board->findPin("D0")->has(s2a::PinCapability::Pwm));
    EXPECT_EQ(board->builtinLed, 2);
    EXPECT_TRUE(board->builtinLedActiveLow);
    EXPECT_TRUE(board->wifi);
    EXPECT_FALSE(board->bluetooth);
    const auto* esp32 = s2a::BoardDatabase::builtin().find("esp32");
    EXPECT_TRUE(esp32->wifi && esp32->bluetooth && !esp32->builtinLedActiveLow);
    EXPECT_FALSE(s2a::BoardDatabase::builtin().find("uno")->wifi);
    // "D13" on an Arduino board is still pin 13.
    EXPECT_EQ(s2a::BoardDatabase::builtin().find("uno")->findPin("D13")->number, 13);
}

} // namespace

// Audit 2026-11-24 (#206): the Linux rule let the user reach Arduino and ESP boards without the
// "dialout" group, but not the Raspberry Pi Pico added in 1.3.0.
TEST(Boards, EveryUsbVendorIsInTheLinuxRule) {
    std::ifstream in(S2A_UDEV_RULES);
    ASSERT_TRUE(in) << S2A_UDEV_RULES;
    const std::string rules((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (const auto& board : s2a::BoardDatabase::builtin().boards()) {
        for (const auto& id : board.usbIds) {
            char vendor[5] = {};
            std::snprintf(vendor, sizeof(vendor), "%04x", id.vendor);
            const std::string rule = std::string("SUBSYSTEM==\"tty\", ATTRS{idVendor}==\"") + vendor + "\"";
            EXPECT_NE(rules.find(rule), std::string::npos) << board.id << ": " << rule;
        }
    }
    // The Pico is uploaded through its BOOTSEL USB device, which is not a serial port.
    EXPECT_NE(rules.find("SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"2e8a\""), std::string::npos);
}
