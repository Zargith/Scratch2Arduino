#include "s2a/wiring.hpp"
#include "s2a/codegen.hpp"

#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <set>
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

TEST(Wiring, MakesOneJoystickOfItsThreeBlocks) {
    const std::map<std::string, std::string> pins{{"X_PIN", "A2"}, {"Y_PIN", "A3"}, {"SW_PIN", "7"}};
    auto with = [&](std::map<std::string, std::string> fields) {
        fields.insert(pins.begin(), pins.end());
        return fields;
    };
    const Plan plan = s2a::wiring::plan(program({block("x", "sensors_joystick", with({{"AXIS", "X"}})),
                                                 block("up", "sensors_joystick_direction", with({{"DIRECTION", "UP"}})),
                                                 block("sw", "sensors_joystick_pressed", with({})),
                                                 block("pir", "sensors_motion", {{"PIN", "3"}}),
                                                 block("ir", "sensors_obstacle", {{"PIN", "4"}})}),
                                        Language::English);
    ASSERT_EQ(plan.components.size(), 3U);
    EXPECT_EQ(plan.components[0].kind, "joystick");
    EXPECT_EQ(plan.components[0].blockIds, (std::vector<std::string>{"x", "up", "sw"}));
    EXPECT_EQ(wires(plan.components[0]),
              (std::vector<std::string>{"GND -> GND", "+5V -> 5V", "VRx -> A2", "VRy -> A3", "SW -> 7"}));
    EXPECT_EQ(wires(plan.components[1]), (std::vector<std::string>{"VCC -> 5V", "OUT -> 3", "GND -> GND"}));
    EXPECT_EQ(wires(plan.components[2]), (std::vector<std::string>{"OUT -> 4", "GND -> GND", "VCC -> 5V"}));
    EXPECT_TRUE(plan.warnings.empty());
    // Drawn as modules, without an unknown part.
    const std::string svg = s2a::wiring::svg(plan, *s2a::BoardDatabase::builtin().find("uno"), "Test");
    for (const char* label : {"PIR", "FC-51"}) {
        EXPECT_NE(svg.find(label), std::string::npos) << label;
    }
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

// Regression: a 16-pin LCD with D6 moved from 8 to 7 (its D7 pin) stopped the wiring window with
// "invalid map<K, T> key" (pins were recorded under an unfinished component key).
TEST(Wiring, WarnsWhenTwoLegsOfOneComponentShareAPin) {
    const std::map<std::string, std::string> pins{{"LINE", "1"}, {"RS_PIN", "12"}, {"E_PIN", "11"}, {"D4_PIN", "10"},
                                                  {"D5_PIN", "9"},  {"D6_PIN", "7"},  {"D7_PIN", "7"}};
    const Project project = program({block("print", "display_lcd16_print", pins)});
    const Plan plan = s2a::wiring::plan(project, Language::English);
    ASSERT_EQ(plan.warnings.size(), 1U);
    EXPECT_EQ(plan.warnings[0].kind, WarningKind::PinUsedTwice);
    EXPECT_NE(plan.warnings[0].message.find("Pin 7"), std::string::npos) << plan.warnings[0].message;
    EXPECT_NE(plan.warnings[0].message.find("D6"), std::string::npos) << plan.warnings[0].message;
    EXPECT_EQ(plan.warnings[0].blockIds, std::vector<std::string>{"print"});
    EXPECT_FALSE(s2a::wiring::svg(plan, *s2a::BoardDatabase::builtin().find("uno"), "Test").empty());
}

TEST(Wiring, WarnsWhenAMultiPinComponentSharesAPinWithAnother) {
    const std::map<std::string, std::string> pins{{"LINE", "1"}, {"RS_PIN", "12"}, {"E_PIN", "11"}, {"D4_PIN", "10"},
                                                  {"D5_PIN", "9"},  {"D6_PIN", "8"},  {"D7_PIN", "7"}};
    const Project project = program({block("print", "display_lcd16_print", pins),
                                     block("led", "pins_digital_write", {{"PIN", "7"}, {"STATE", "HIGH"}})});
    const Plan plan = s2a::wiring::plan(project, Language::English);
    ASSERT_EQ(plan.warnings.size(), 1U);
    EXPECT_EQ(plan.warnings[0].kind, WarningKind::PinUsedTwice);
    EXPECT_EQ(plan.warnings[0].blockIds.size(), 2U);
    EXPECT_FALSE(s2a::wiring::svg(plan, *s2a::BoardDatabase::builtin().find("uno"), "Test").empty());
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

TEST(Wiring, TellsWhatEachBoardPinIsFor) {
    // The simulation finds the Trig pin of a distance sensor by its role, whatever the language.
    const Plan plan = s2a::wiring::plan(
        program({block("distance", "sensors_distance", {{"TRIG_PIN", "7"}, {"ECHO_PIN", "8"}})}, "uno"), Language::French);
    ASSERT_EQ(plan.components.size(), 1U);
    std::vector<std::string> roles;
    for (const auto& connection : plan.components[0].connections) {
        roles.push_back(connection.role + "=" + connection.boardPin);
    }
    EXPECT_EQ(roles, (std::vector<std::string>{"5V=5V", "TRIG_PIN=7", "ECHO_PIN=8", "GND=GND"}));
}

} // namespace

namespace {

TEST(WiringDiagram, DrawsEveryPinOfEveryBoard) {
    const std::set<std::string> power = {"5V", "3V3", "3.3V", "GND", "VIN", "AREF", "IOREF", "RESET", "RST", "EN",
                                         "REF", "RSV", "SD3", "SD2", "SD1", "CMD", "SD0", "CLK", "SCL", "SDA"};
    for (const auto& board : s2a::BoardDatabase::builtin().boards()) {
        ASSERT_GT(board.drawing.width, 0) << board.id;
        std::set<std::string> drawn;
        for (const auto& header : board.drawing.headers) {
            for (const auto& entry : header.pins) {
                if (!entry) {
                    continue;
                }
                drawn.insert(entry->pin);
                EXPECT_TRUE(entry->pin == "-" || power.count(entry->pin) != 0 || board.findPin(entry->pin) != nullptr)
                    << board.id << ": " << entry->pin << " is not a pin of the board";
            }
        }
        for (const auto& pin : board.pins) {
            EXPECT_EQ(drawn.count(pin.label), 1U) << board.id << ": pin " << pin.label << " is not drawn";
        }
        EXPECT_EQ(drawn.count(board.voltage >= 4.5 ? "5V" : "3V3"), 1U) << board.id;
        EXPECT_EQ(drawn.count("GND"), 1U) << board.id;
    }
}

TEST(WiringDiagram, DrawsTheBoardTheBreadboardThePartsAndTheirWires) {
    std::ifstream file(std::string(S2A_EXAMPLES_DIR) + "/distance-alarm/project.json");
    std::stringstream content;
    content << file.rdbuf();
    const auto project = fromJson(content.str());
    ASSERT_TRUE(project.ok());
    const auto* uno = s2a::BoardDatabase::builtin().find("uno");
    const Plan plan = s2a::wiring::plan(project.value(), BlockCatalog::builtin(), *uno, Language::English);
    const std::string svg = s2a::wiring::svg(plan, *uno, "Alarm & <test>");
    EXPECT_EQ(svg.rfind("<svg ", 0), 0U);
    EXPECT_NE(svg.find("</svg>"), std::string::npos);
    EXPECT_NE(svg.find("Alarm &amp; &lt;test&gt;"), std::string::npos);
    // The board, the module of the sensor with its pin names, the resistors, the numbered parts.
    for (const char* expected : {">UNO<", ">HC-SR04<", ">Trig<", ">Echo<", ">220 Ω<", ">1<", ">2<", ">3<", ">30<"}) {
        EXPECT_NE(svg.find(expected), std::string::npos) << expected;
    }
    // The pins used are circled: Trig, Echo, the 3 colours, the buzzer, and the 5V and GND feeding the rails.
    std::size_t circled = 0;
    for (auto at = svg.find(R"(fill="none" stroke="#FFEB3B")"); at != std::string::npos;
         at = svg.find(R"(fill="none" stroke="#FFEB3B")", at + 1)) {
        ++circled;
    }
    EXPECT_EQ(circled, 8U);
}

TEST(WiringDiagram, PlugsSmallPartsIntoTheBreadboardAndPutsModulesAbove) {
    const auto* uno = s2a::BoardDatabase::builtin().find("uno");
    const Plan plan = s2a::wiring::plan(program({block("led", "pins_digital_write", {{"PIN", "7"}}),
                                                 block("servo", "motors_servo_write", {{"PIN", "9"}})}),
                                        BlockCatalog::builtin(), *uno, Language::English);
    const std::string svg = s2a::wiring::svg(plan, *uno, "Test");
    EXPECT_NE(svg.find(">SG90<"), std::string::npos); // the servo is a module
    EXPECT_NE(svg.find(">S<"), std::string::npos);    // with the short name of its signal pin
    EXPECT_NE(svg.find(">220 Ω<"), std::string::npos); // the LED's resistor on the breadboard
}

TEST(WiringDiagram, AddsThePinsMissingFromTheDrawing) {
    s2a::Board board = *s2a::BoardDatabase::builtin().find("uno");
    board.drawing.headers.clear();
    const Plan plan = s2a::wiring::plan(program({block("led", "pins_digital_write", {{"PIN", "7"}})}),
                                        BlockCatalog::builtin(), board, Language::English);
    const std::string svg = s2a::wiring::svg(plan, board, "Test");
    EXPECT_NE(svg.find(">7<"), std::string::npos);
    EXPECT_NE(svg.find(">GND<"), std::string::npos);
}

} // namespace

namespace {

/// "kind pin..." of each component of a plan, for short comparisons.
std::vector<std::string> summary(const Plan& plan) {
    std::vector<std::string> lines;
    for (const auto& component : plan.components) {
        std::string line = component.kind;
        for (const auto& connection : component.connections) {
            if (connection.boardPin != "5V" && connection.boardPin != "GND" && connection.boardPin != "VIN" &&
                connection.boardPin != "3V3") {
                line += " " + connection.boardPin;
            }
        }
        lines.push_back(line);
    }
    return lines;
}

Project textProject(ProgramLanguage language, std::string code) {
    Project project;
    project.boardId = "uno";
    project.language = language;
    project.code = std::move(code);
    return project;
}

TEST(CodeWiring, FindsTheComponentsOfAHandWrittenSketch) {
    const auto plan = s2a::wiring::plan(textProject(ProgramLanguage::Cpp, R"cpp(#include <Servo.h>
#include <DHT.h>
#define BUTTON 2
const int led = 13;   // the LED
int pot = A0;
Servo arm;
DHT sensor(4, DHT22);
/* pinMode(7, OUTPUT); in a comment: not a component */
void setup() {
  pinMode(led, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  arm.attach(9);
  sensor.begin();
  pinMode(6, OUTPUT);
  Serial.println("digitalWrite(12, HIGH)");
}
void loop() {
  if (digitalRead(BUTTON) == LOW) {
    digitalWrite(led, HIGH);
  }
  arm.write(map(analogRead(pot), 0, 1023, 0, 180));
  digitalWrite(6, HIGH);
  delayMicroseconds(10);
  digitalWrite(6, LOW);
  long echo = pulseIn(5, HIGH);
  tone(8, 440, 100);
}
)cpp"),
                                          Language::English);
    // In the order of the code.
    EXPECT_EQ(summary(plan), (std::vector<std::string>{"dht 4", "output 13", "button 2", "servo 9", "analog_input A0",
                                                       "ultrasonic 6 5", "buzzer 8"}));
    ASSERT_FALSE(plan.components.empty());
    EXPECT_EQ(plan.components[0].blockIds, std::vector<std::string>{"code-7"}) << "the line of the component";
}

TEST(CodeWiring, ReadsPythonAndJavaScriptPrograms) {
    const auto python = s2a::wiring::plan(textProject(ProgramLanguage::Python, "LED = 13\n"
                                                                              "pinMode(LED, OUTPUT)\n"
                                                                              "while True:\n"
                                                                              "    digitalWrite(LED, HIGH)\n"
                                                                              "    servoWrite(9, analogRead(A1) / 6)\n"),
                                          Language::English);
    EXPECT_EQ(summary(python), (std::vector<std::string>{"output 13", "servo 9", "analog_input A1"}));
    const auto javascript = s2a::wiring::plan(
        textProject(ProgramLanguage::JavaScript, "pinMode(2, INPUT);\nif (digitalRead(2) == HIGH) { tone(8, 440); }\n"),
        Language::English);
    EXPECT_EQ(summary(javascript), (std::vector<std::string>{"input 2", "buzzer 8"}));
}

TEST(CodeWiring, LeavesOutPinsThatChange) {
    const auto plan = s2a::wiring::plan(textProject(ProgramLanguage::Cpp, "int led = 3;\n"
                                                                          "void setup() {}\n"
                                                                          "void loop() {\n"
                                                                          "  for (int pin = 3; pin < 6; pin++) digitalWrite(pin, HIGH);\n"
                                                                          "  led = led + 1;\n"
                                                                          "  digitalWrite(led, HIGH);\n"
                                                                          "  digitalWrite(LED_BUILTIN, HIGH);\n"
                                                                          "}\n"),
                                        Language::English);
    EXPECT_TRUE(plan.components.empty()) << summary(plan).front();
}

TEST(CodeWiring, AddsMyCodeToTheBlocksAndIgnoresTheBlocksOfATextProgram) {
    Project project;
    project.boardId = "uno";
    Block start;
    start.id = "hat";
    start.type = "events_when_program_starts";
    Block servo;
    servo.id = "servo";
    servo.type = "motors_servo_write";
    servo.fields["PIN"] = "9";
    project.scripts.push_back({0, 0, {start, servo}});
    project.userCode = UserCode{ProgramLanguage::Cpp, "void beep() { tone(8, 440, 50); }\n"};
    EXPECT_EQ(summary(s2a::wiring::plan(project, Language::English)), (std::vector<std::string>{"servo 9", "buzzer 8"}));

    project.language = ProgramLanguage::Cpp; // expert mode: the code is the program
    project.code = "void setup() { pinMode(2, INPUT_PULLUP); }\nvoid loop() { digitalRead(2); }\n";
    EXPECT_EQ(summary(s2a::wiring::plan(project, Language::English)), std::vector<std::string>{"button 2"});
}

TEST(CodeWiring, FindsTheComponentsOfTheCodeOfTheBlocks) {
    // The expert mode starts from the code of the blocks: its components must be found again.
    Project project;
    project.boardId = "uno";
    Block start;
    start.id = "hat";
    start.type = "events_when_program_starts";
    project.scripts.push_back({0, 0, {start}});
    const auto add = [&](const char* id, const char* type, std::map<std::string, std::string> fields) {
        Block block;
        block.id = id;
        block.type = type;
        block.fields = std::move(fields);
        project.scripts[0].blocks.push_back(block);
    };
    add("servo", "motors_servo_write", {{"PIN", "9"}});
    add("motor", "motors_dc_run", {{"EN_PIN", "6"}, {"IN1_PIN", "7"}, {"IN2_PIN", "4"}});
    add("tone", "sound_play_tone", {{"PIN", "8"}});
    add("matrix", "lights_matrix_show", {{"DIN_PIN", "12"}, {"CLK_PIN", "11"}, {"CS_PIN", "10"}});
    add("strip", "lights_neopixel_fill", {{"PIN", "5"}});
    add("lcd", "display_lcd16_print", {{"RS_PIN", "A0"}, {"E_PIN", "A1"}, {"D4_PIN", "A2"}, {"D5_PIN", "A3"}, {"D6_PIN", "A4"}, {"D7_PIN", "A5"}});
    const Project text = textProject(ProgramLanguage::Cpp, s2a::codegen::generate(project).source);
    auto fromBlocks = summary(s2a::wiring::plan(project, Language::English));
    auto fromCode = summary(s2a::wiring::plan(text, Language::English));
    std::sort(fromBlocks.begin(), fromBlocks.end());
    std::sort(fromCode.begin(), fromCode.end());
    EXPECT_EQ(fromCode, fromBlocks);
}

} // namespace
