#include "s2a/wiring.hpp"

#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace {

using namespace s2a::project;
using s2a::BlockCatalog;
using s2a::Language;
using s2a::wiring::Plan;
using s2a::wiring::WarningKind;

Block block(std::string id, std::string type, std::map<std::string, std::string> fields = {}) {
    Block result;
    result.id = std::move(id);
    result.type = std::move(type);
    result.fields = std::move(fields);
    return result;
}

Project program(Stack blocks, std::string board = "uno") {
    Project project;
    project.boardId = std::move(board);
    blocks.insert(blocks.begin(), block("hat", "events_when_program_starts"));
    project.scripts.push_back({0, 0, std::move(blocks)});
    return project;
}

/// "component pin -> board pin [via]" lines of a component, to compare in one go.
std::vector<std::string> wires(const s2a::wiring::Component& component) {
    std::vector<std::string> lines;
    for (const auto& connection : component.connections) {
        lines.push_back(connection.componentPin + " -> " + connection.boardPin +
                        (connection.via.empty() ? "" : " [" + connection.via + "]"));
    }
    return lines;
}

TEST(Wiring, DescribesTheComponentOfEveryCatalogBlock) {
    const auto kinds = s2a::wiring::componentKinds();
    for (const auto& category : BlockCatalog::builtin().categories()) {
        for (const auto* definition : BlockCatalog::builtin().blocksIn(category.id)) {
            if (definition->component.empty()) {
                continue;
            }
            ASSERT_NE(std::find(kinds.begin(), kinds.end(), definition->component), kinds.end())
                << definition->type << ": " << definition->component;
            for (const std::string& pin : s2a::wiring::blockPins(definition->component)) {
                const auto* arg = definition->findArg(pin);
                ASSERT_NE(arg, nullptr) << definition->type << " has no " << pin;
                EXPECT_EQ(arg->kind, s2a::ArgKind::Pin) << definition->type << " " << pin;
            }
        }
    }
}

TEST(Wiring, ListsTheComponentsOfAProjectWithTheirPins) {
    std::ifstream file(std::string(S2A_EXAMPLES_DIR) + "/distance-alarm/project.json");
    std::stringstream content;
    content << file.rdbuf();
    const auto project = fromJson(content.str());
    ASSERT_TRUE(project.ok());
    const Plan plan = s2a::wiring::plan(project.value(), Language::English);
    ASSERT_EQ(plan.components.size(), 3U);
    EXPECT_EQ(plan.components[0].kind, "ultrasonic");
    EXPECT_EQ(wires(plan.components[0]),
              (std::vector<std::string>{"VCC -> 5V", "Trig -> 12", "Echo -> 11", "GND -> GND"}));
    EXPECT_EQ(plan.components[1].kind, "rgb_led");
    EXPECT_EQ(wires(plan.components[1]),
              (std::vector<std::string>{"R (red) -> 9 [220 Ω resistor]", "G (green) -> 10 [220 Ω resistor]",
                                        "B (blue) -> 6 [220 Ω resistor]", "− (the longest) -> GND"}));
    EXPECT_GT(plan.components[1].blockIds.size(), 1U); // several colour blocks, one LED
    EXPECT_EQ(plan.components[2].kind, "buzzer");
    EXPECT_TRUE(plan.warnings.empty());
}

TEST(Wiring, MergesTheBlocksOfOneComponentAndIgnoresLooseBlocks) {
    Project project = program({block("start", "sound_start_tone", {{"PIN", "8"}}), block("stop", "sound_stop", {{"PIN", "8"}}),
                               block("other", "sound_stop", {{"PIN", "7"}})});
    project.scripts.push_back({300, 0, {block("loose", "motors_servo_write", {{"PIN", "9"}})}});
    const Plan plan = s2a::wiring::plan(project, Language::French);
    ASSERT_EQ(plan.components.size(), 2U);
    EXPECT_EQ(plan.components[0].name, "Buzzer passif");
    EXPECT_EQ(plan.components[0].blockIds, (std::vector<std::string>{"start", "stop"}));
    EXPECT_EQ(plan.components[1].blockIds, std::vector<std::string>{"other"});
}

TEST(Wiring, WarnsAboutAPinUsedTwice) {
    const Plan plan = s2a::wiring::plan(
        program({block("led", "pins_digital_write", {{"PIN", "7"}, {"STATE", "HIGH"}}),
                 block("button", "sensors_button_pressed", {{"PIN", "7"}})}),
        Language::English);
    ASSERT_EQ(plan.warnings.size(), 1U);
    EXPECT_EQ(plan.warnings[0].kind, WarningKind::PinUsedTwice);
    EXPECT_NE(plan.warnings[0].message.find("Pin 7"), std::string::npos) << plan.warnings[0].message;
    EXPECT_EQ(plan.warnings[0].blockIds.size(), 2U);
}

TEST(Wiring, WarnsWhenAScreenSharesItsI2cPins) {
    Block print = block("print", "display_lcd_print", {{"LINE", "1"}});
    const Plan plan = s2a::wiring::plan(program({print, block("led", "pins_digital_write", {{"PIN", "A4"}})}),
                                        Language::English);
    ASSERT_EQ(plan.components.size(), 2U);
    EXPECT_EQ(wires(plan.components[0]), (std::vector<std::string>{"GND -> GND", "VCC -> 5V", "SDA -> A4", "SCL -> A5"}));
    ASSERT_EQ(plan.warnings.size(), 1U);
    EXPECT_EQ(plan.warnings[0].kind, WarningKind::I2cPinTaken);
    EXPECT_EQ(plan.warnings[0].blockIds, std::vector<std::string>{"led"});
}

TEST(Wiring, WarnsWhenUsbCannotPowerEverything) {
    const Plan plan = s2a::wiring::plan(program({block("a", "motors_servo_write", {{"PIN", "9"}}),
                                                 block("b", "motors_servo_write", {{"PIN", "10"}}),
                                                 block("c", "lights_neopixel_fill", {{"PIN", "6"}})}),
                                        Language::English);
    ASSERT_EQ(plan.warnings.size(), 1U);
    EXPECT_EQ(plan.warnings[0].kind, WarningKind::PowerBudget);
    EXPECT_NE(plan.warnings[0].message.find("800 mA"), std::string::npos) << plan.warnings[0].message;
}

TEST(Wiring, Uses3v3AndVinOnAnEsp32) {
    const Plan plan = s2a::wiring::plan(program({block("knob", "sensors_potentiometer", {{"PIN", "34"}}),
                                                 block("far", "sensors_distance", {{"TRIG_PIN", "5"}, {"ECHO_PIN", "18"}})},
                                                "esp32"),
                                        Language::English);
    ASSERT_EQ(plan.components.size(), 2U);
    EXPECT_EQ(wires(plan.components[0]),
              (std::vector<std::string>{"left leg -> GND", "middle leg -> 34", "right leg -> 3V3"}));
    EXPECT_EQ(wires(plan.components[1])[0], "VCC -> VIN");
    EXPECT_NE(plan.components[1].note.find("voltage divider"), std::string::npos);
}

TEST(Wiring, KnowsTheNamesOfTheNodeMcuPins) {
    const Plan plan = s2a::wiring::plan(program({block("led", "pins_digital_write", {{"PIN", "5"}})}, "esp8266"),
                                        Language::English);
    ASSERT_EQ(plan.components.size(), 1U);
    EXPECT_EQ(plan.components[0].connections[0].boardPin, "D1");
}

} // namespace
