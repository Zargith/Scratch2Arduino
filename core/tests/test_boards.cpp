#include "s2a/boards.hpp"

#include <gtest/gtest.h>
#include <string>

namespace {

using s2a::BoardDatabase;
using s2a::PinCapability;

std::vector<int> numbers(const std::vector<const s2a::Pin*>& pins) {
    std::vector<int> result;
    for (const auto* pin : pins) {
        result.push_back(pin->number);
    }
    return result;
}

TEST(BuiltinBoards, LoadsUnoNanoAndMega) {
    const auto& database = BoardDatabase::builtin();
    ASSERT_EQ(database.boards().size(), 3U);
    EXPECT_NE(database.find("uno"), nullptr);
    EXPECT_NE(database.find("nano"), nullptr);
    EXPECT_NE(database.find("mega"), nullptr);
    EXPECT_EQ(database.find("leonardo"), nullptr);
    EXPECT_NE(database.find(BoardDatabase::kDefaultBoardId), nullptr);
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
