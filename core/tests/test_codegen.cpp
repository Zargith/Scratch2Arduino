#include "s2a/codegen.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace s2a::project;
using s2a::codegen::generate;

Block block(std::string id, std::string type) {
    Block result;
    result.id = std::move(id);
    result.type = std::move(type);
    return result;
}

Block wait(std::string id, std::string seconds) {
    Block result = block(std::move(id), "control_wait");
    result.inputs["SECONDS"].value = std::move(seconds);
    return result;
}

Block led(std::string id, std::string state) {
    Block result = block(std::move(id), "pins_builtin_led");
    result.fields["STATE"] = std::move(state);
    return result;
}

Project withScript(Stack blocks, std::string boardId = "uno") {
    Project project;
    project.name = "Test";
    project.boardId = std::move(boardId);
    project.scripts.push_back({0, 0, std::move(blocks)});
    return project;
}

Project blink() {
    Block forever = block("loop", "control_forever");
    forever.statements["SUBSTACK"] = {led("on", "ON"), wait("w1", "0,5"), led("off", "OFF"),
                                      wait("w2", "0.5")};
    return withScript({block("hat", "events_when_program_starts"), forever});
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        result.push_back(line);
    }
    return result;
}

TEST(Codegen, GeneratesBlinkAsSetupAndLoop) {
    const auto sketch = generate(blink());
    EXPECT_EQ(sketch.source, "// Scratch2Arduino: Test\n"
                             "// Arduino Uno\n"
                             "\n"
                             "void s2a_wait(double seconds) {\n"
                             "  if (seconds > 0) {\n"
                             "    delay((unsigned long)(seconds * 1000.0));\n"
                             "  }\n"
                             "}\n"
                             "\n"
                             "void setup() {\n"
                             "  pinMode(LED_BUILTIN, OUTPUT);\n"
                             "}\n"
                             "\n"
                             "void loop() {\n"
                             "  digitalWrite(LED_BUILTIN, HIGH);\n"
                             "  s2a_wait(0.5);\n"
                             "  digitalWrite(LED_BUILTIN, LOW);\n"
                             "  s2a_wait(0.5);\n"
                             "}\n");
}

TEST(Codegen, MapsBlocksToLines) {
    const auto sketch = generate(blink());
    const auto source = lines(sketch.source);
    for (const char* id : {"on", "w1", "off", "w2"}) {
        ASSERT_EQ(sketch.blockLines.count(id), 1U) << id;
    }
    EXPECT_EQ(source.at(sketch.blockLines.at("on") - 1), "  digitalWrite(LED_BUILTIN, HIGH);");
    EXPECT_EQ(source.at(sketch.blockLines.at("w2") - 1), "  s2a_wait(0.5);");
}

TEST(Codegen, IgnoresScriptsWithoutHat) {
    Project project = blink();
    project.scripts.push_back({10, 10, {led("loose", "ON")}});
    const auto sketch = generate(project);
    EXPECT_EQ(sketch.blockLines.count("loose"), 0U);
}

TEST(Codegen, KeepsForeverInsideSetupWhenNotLast) {
    Block forever = block("f", "control_forever");
    forever.statements["SUBSTACK"] = {led("on", "ON")};
    const auto sketch =
        generate(withScript({block("hat", "events_when_program_starts"), forever, led("after", "OFF")}));
    EXPECT_NE(sketch.source.find("  for (;;) {\n    digitalWrite(LED_BUILTIN, HIGH);\n    yield();\n  }\n"),
              std::string::npos)
        << sketch.source;
}

TEST(Codegen, DeclaresPinModesFromUsage) {
    Block write = block("w", "pins_digital_write");
    write.fields = {{"PIN", "7"}, {"STATE", "HIGH"}};
    Block read = block("r", "pins_digital_read");
    read.fields["PIN"] = "2";
    Block condition = block("if", "control_if");
    condition.inputs["CONDITION"].block = read;
    condition.statements["SUBSTACK"] = {write};
    const auto sketch = generate(withScript({block("hat", "events_when_program_starts"), condition}));
    EXPECT_NE(sketch.source.find("pinMode(2, INPUT);"), std::string::npos);
    EXPECT_NE(sketch.source.find("pinMode(7, OUTPUT);"), std::string::npos);
    EXPECT_NE(sketch.source.find("  if (digitalRead(2) == HIGH) {\n    digitalWrite(7, HIGH);\n  }"),
              std::string::npos)
        << sketch.source;
}

TEST(Codegen, ReadsTheChosenStateWithThePinMode) {
    Block mode = block("m", "pins_set_mode");
    mode.fields = {{"PIN", "2"}, {"MODE", "INPUT_PULLUP"}};
    Block write = block("w", "pins_digital_write");
    write.fields = {{"PIN", "7"}, {"STATE", "HIGH"}};
    Block read = block("r", "pins_digital_read");
    read.fields = {{"PIN", "2"}, {"STATE", "LOW"}};
    Block condition = block("if", "control_if");
    condition.inputs["CONDITION"].block = read;
    condition.statements["SUBSTACK"] = {write};
    const auto sketch = generate(withScript({block("hat", "events_when_program_starts"), mode, condition}));
    // Declared in setup() (before any read) and where the block is.
    EXPECT_NE(sketch.source.find("void setup() {\n  pinMode(2, INPUT_PULLUP);"), std::string::npos) << sketch.source;
    EXPECT_EQ(sketch.source.find("pinMode(2, INPUT);"), std::string::npos) << sketch.source;
    EXPECT_NE(sketch.source.find("  if (digitalRead(2) == LOW) {"), std::string::npos) << sketch.source;
}

TEST(Codegen, ReadsHighWhenTheStateIsMissing) {
    Block read = block("r", "pins_digital_read"); // projects saved before the state menu existed
    read.fields["PIN"] = "4";
    Block condition = block("if", "control_if");
    condition.inputs["CONDITION"].block = read;
    const auto sketch = generate(withScript({block("hat", "events_when_program_starts"), condition}));
    EXPECT_NE(sketch.source.find("if (digitalRead(4) == HIGH) {"), std::string::npos) << sketch.source;
    EXPECT_NE(sketch.source.find("pinMode(4, INPUT);"), std::string::npos) << sketch.source;
}

TEST(Codegen, UsesSafeDefaultsForMissingOrInvalidInputs) {
    Block write = block("w", "pins_analog_write");
    write.fields["PIN"] = "7; system()"; // not a pin: falls back to the default pin
    write.inputs["VALUE"].value = "beaucoup";
    Block condition = block("if", "control_if"); // no condition plugged in
    const auto sketch =
        generate(withScript({block("hat", "events_when_program_starts"), write, condition}));
    EXPECT_EQ(sketch.source.find("system"), std::string::npos);
    EXPECT_NE(sketch.source.find("constrain((int)(0), 0, 255)"), std::string::npos) << sketch.source;
    EXPECT_NE(sketch.source.find("if (false) {"), std::string::npos);
}

TEST(Codegen, GeneratesExpressionsAndVariables) {
    Project project;
    project.name = "Calcul";
    project.variables = {{"v1", "vitesse max", 2.5}, {"v2", "vitesse-max", 0}};
    Block sum = block("sum", "operators_add");
    sum.inputs["NUM1"].value = "1";
    Block get = block("get", "variables_get");
    get.fields["VARIABLE"] = "v1";
    sum.inputs["NUM2"].block = get;
    Block set = block("set", "variables_set");
    set.fields["VARIABLE"] = "v2";
    set.inputs["VALUE"].block = sum;
    Block repeat = block("rep", "control_repeat");
    repeat.inputs["TIMES"].value = "3";
    repeat.statements["SUBSTACK"] = {set};
    project.scripts.push_back({0, 0, {block("hat", "events_when_program_starts"), repeat}});

    const auto sketch = generate(project);
    EXPECT_NE(sketch.source.find("double var_vitesse_max = 2.5; // vitesse max"), std::string::npos);
    EXPECT_NE(sketch.source.find("double var_vitesse_max_2 = 0; // vitesse-max"), std::string::npos);
    EXPECT_NE(sketch.source.find("  for (long i1 = 0; i1 < (long)(3); ++i1) {\n"
                                 "    var_vitesse_max_2 = (1 + var_vitesse_max);\n  }"),
              std::string::npos)
        << sketch.source;
}

TEST(Codegen, DividesLikeScratch) {
    Block divide = block("d", "operators_divide");
    divide.inputs["NUM1"].value = "1";
    divide.inputs["NUM2"].value = "2";
    Block wait = block("w", "control_wait");
    wait.inputs["SECONDS"].block = divide;
    const auto sketch = generate(withScript({block("hat", "events_when_program_starts"), wait}));
    EXPECT_NE(sketch.source.find("s2a_wait(((double)1 / 2));"), std::string::npos) << sketch.source;
}

TEST(Codegen, AddsHelpersOnlyWhenUsed) {
    EXPECT_EQ(generate(blink()).source.find("s2a_random"), std::string::npos);
    Block random = block("r", "operators_random");
    random.inputs["FROM"].value = "1";
    random.inputs["TO"].value = "6";
    Block write = block("w", "pins_analog_write");
    write.fields["PIN"] = "9";
    write.inputs["VALUE"].block = random;
    const auto sketch = generate(withScript({block("hat", "events_when_program_starts"), write}));
    EXPECT_NE(sketch.source.find("long s2a_random("), std::string::npos);
    EXPECT_NE(sketch.source.find("randomSeed(micros());"), std::string::npos);
}

TEST(Codegen, RunsSeveralScriptsSideBySide) {
    // Two "when the program starts" scripts must blink at their own pace, as in Scratch.
    Project project = blink();
    Block forever = block("loop2", "control_forever");
    Block repeat = block("repeat", "control_repeat");
    repeat.inputs["TIMES"].value = "3";
    repeat.statements["SUBSTACK"] = {wait("w3", "0.3")};
    forever.statements["SUBSTACK"] = {repeat};
    project.scripts.push_back({300, 0, {block("hat2", "events_when_program_starts"), forever}});
    const auto source = generate(project).source;
    for (const char* expected : {"void s2a_script1() {", "void s2a_script2() {", "  s2a_script1();\n  s2a_script2();\n}",
                                 "S2A_WAIT(task, 0.5);", "S2A_WAIT(task, 0.3);", "long s2a_i1;",
                                 "for (s2a_i1 = 0; s2a_i1 < (long)(3); ++s2a_i1) {", "S2A_YIELD(task);"}) {
        EXPECT_NE(source.find(expected), std::string::npos) << expected << "\n" << source;
    }
    EXPECT_EQ(source.find("delay("), std::string::npos) << "a wait must not block the other script";
    // Block lines still point at the code of each block.
    const auto sketch = generate(project);
    EXPECT_EQ(lines(sketch.source).at(sketch.blockLines.at("w3") - 1), "      S2A_WAIT(task, 0.3);");
}

TEST(Codegen, KeepsTheClassicFormForASingleScript) {
    const auto source = generate(blink()).source;
    EXPECT_EQ(source.find("S2A_"), std::string::npos);
    EXPECT_NE(source.find("void loop() {\n  digitalWrite(LED_BUILTIN, HIGH);"), std::string::npos);
}

TEST(Codegen, NumberLiterals) {
    using s2a::codegen::numberLiteral;
    EXPECT_EQ(numberLiteral("12"), "12");
    EXPECT_EQ(numberLiteral(" 1,5 "), "1.5");
    EXPECT_EQ(numberLiteral("-0.25"), "-0.25");
    EXPECT_EQ(numberLiteral(""), "0");
    EXPECT_EQ(numberLiteral("abc"), "0");
    EXPECT_EQ(numberLiteral("1e999"), "0");
    EXPECT_EQ(numberLiteral("3)); evil("), "0");
}

TEST(Codegen, Identifiers) {
    using s2a::codegen::identifier;
    EXPECT_EQ(identifier("score"), "score");
    EXPECT_EQ(identifier("vitesse max"), "vitesse_max");
    EXPECT_EQ(identifier("2 joueurs"), "_2_joueurs");
    EXPECT_EQ(identifier("luminosité"), "luminosite");
    EXPECT_EQ(identifier("Ça marche"), "Ca_marche");
    EXPECT_EQ(identifier("😀"), "variable");
    EXPECT_EQ(identifier("a😀b"), "a_b");
}

} // namespace
