#include "s2a/validator.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace {

using namespace s2a::project;
using s2a::Language;
using s2a::validation::Issue;
using s2a::validation::Rule;
using s2a::validation::Severity;

Block block(std::string id, std::string type) {
    Block result;
    result.id = std::move(id);
    result.type = std::move(type);
    return result;
}

Block hat() {
    return block("hat", "events_when_program_starts");
}

Block led(std::string id = "led") {
    Block result = block(std::move(id), "pins_builtin_led");
    result.fields["STATE"] = "ON";
    return result;
}

Project program(Stack blocks) {
    Project project;
    project.boardId = "uno";
    project.scripts.push_back({0, 0, std::move(blocks)});
    return project;
}

std::vector<Issue> check(const Project& project, Language language = Language::English) {
    return s2a::validation::validate(project, language);
}

/// The only issue of `issues`.
Issue single(const std::vector<Issue>& issues) {
    if (issues.size() != 1U) {
        throw std::runtime_error("expected one issue, got " + std::to_string(issues.size()));
    }
    return issues.front();
}

TEST(Validator, AcceptsACorrectProgram) {
    Block forever = block("loop", "control_forever");
    forever.statements["SUBSTACK"] = {led()};
    EXPECT_TRUE(check(program({hat(), forever})).empty());
}

TEST(Validator, AcceptsTheExampleProjects) {
    for (const char* name : {"blink", "all-blocks", "esp32-dimmer", "wireless-station-wifi", "wireless-station-bluetooth", "phone-lamp"}) {
        std::ifstream file(std::string(S2A_EXAMPLES_DIR) + "/" + name + "/project.json");
        std::stringstream content;
        content << file.rdbuf();
        const auto project = fromJson(content.str());
        ASSERT_TRUE(project.ok()) << name;
        EXPECT_FALSE(s2a::validation::hasErrors(check(project.value()))) << name;
    }
}

TEST(Validator, NoStartBlock) {
    Project project = program({led()});
    const auto issues = check(project);
    ASSERT_EQ(issues.size(), 1U); // the loose blocks themselves are not reported
    EXPECT_EQ(issues[0].rule, Rule::NoStartBlock);
    EXPECT_EQ(issues[0].severity, Severity::Error);
    EXPECT_NE(s2a::validation::describe(issues[0], Language::English).find("when the program starts"), std::string::npos);
}

TEST(Validator, LooseBlocksAreLikeComments) {
    Project project = program({hat(), led()});
    Block bad = block("bad", "control_if"); // no condition, but it never runs
    project.scripts.push_back({300, 0, {bad}});
    EXPECT_TRUE(check(project).empty());
}

Block define(std::string id, std::string function) {
    Block result = block(std::move(id), "functions_define");
    result.fields["FUNCTION"] = std::move(function);
    return result;
}

Block call(std::string id, std::string function) {
    Block result = block(std::move(id), "functions_call");
    result.fields["FUNCTION"] = std::move(function);
    return result;
}

Project withFunctions(Stack main, std::vector<Stack> definitions) {
    Project project = program(std::move(main));
    project.functions = {{"f", "blink"}, {"g", "beep"}};
    for (Stack& stack : definitions) {
        project.scripts.push_back({300, 0, std::move(stack)});
    }
    return project;
}

TEST(Validator, AcceptsFunctions) {
    EXPECT_TRUE(check(withFunctions({hat(), call("c", "f")}, {{define("d", "f"), led(), call("c2", "g")},
                                                             {define("d2", "g"), led("led2")}}))
                    .empty());
}

TEST(Validator, FunctionsAloneDoNotStartTheProgram) {
    Project project = withFunctions({define("d", "f"), led()}, {});
    EXPECT_EQ(single(check(project)).rule, Rule::NoStartBlock);
}

TEST(Validator, MissingFunction) {
    const Issue issue = single(check(withFunctions({hat(), call("c", "deleted")}, {})));
    EXPECT_EQ(issue.rule, Rule::MissingFunction);
    EXPECT_EQ(issue.blockId, "c");
}

TEST(Validator, FunctionNotDefined) {
    const Issue issue = single(check(withFunctions({hat(), call("c", "f")}, {})));
    EXPECT_EQ(issue.rule, Rule::FunctionNotDefined);
    EXPECT_NE(s2a::validation::describe(issue, Language::French).find("\"définir blink\""), std::string::npos);
}

TEST(Validator, FunctionDefinedTwice) {
    const Issue issue =
        single(check(withFunctions({hat(), call("c", "f")}, {{define("d1", "f"), led()}, {define("d2", "f"), led("l2")}})));
    EXPECT_EQ(issue.rule, Rule::FunctionDefinedTwice);
    EXPECT_EQ(issue.blockId, "d2");
}

TEST(Validator, RecursiveFunction) {
    // f calls g, g calls f: neither would ever end.
    const auto issues = check(withFunctions({hat(), call("c", "f")}, {{define("d1", "f"), call("cg", "g")},
                                                                     {define("d2", "g"), call("cf", "f")}}));
    ASSERT_EQ(issues.size(), 2U);
    EXPECT_EQ(issues[0].rule, Rule::RecursiveFunction);
    EXPECT_EQ(issues[0].blockId, "cg");
    EXPECT_EQ(issues[1].blockId, "cf");
}

TEST(Validator, EmptyScript) {
    const Issue issue = single(check(program({hat()})));
    EXPECT_EQ(issue.rule, Rule::EmptyScript);
    EXPECT_EQ(issue.severity, Severity::Warning);
}

TEST(Validator, EmptyMouth) {
    // Block names are translated when validating: validate and describe in the same language.
    const Issue issue = single(check(program({hat(), block("repeat", "control_repeat")}), Language::French));
    EXPECT_EQ(issue.rule, Rule::EmptyMouth);
    EXPECT_EQ(issue.blockId, "repeat");
    EXPECT_NE(s2a::validation::describe(issue, Language::French).find("\"répéter ... fois\" est vide"), std::string::npos)
        << s2a::validation::describe(issue, Language::French);
}

TEST(Validator, BlocksAfterForever) {
    Block forever = block("loop", "control_forever");
    forever.statements["SUBSTACK"] = {led()};
    const Issue issue = single(check(program({hat(), forever, led("after")})));
    EXPECT_EQ(issue.rule, Rule::BlocksAfterForever);
    EXPECT_EQ(issue.blockId, "after");
}

TEST(Validator, MissingCondition) {
    Block condition = block("if", "control_if");
    condition.statements["SUBSTACK"] = {led()};
    const Issue issue = single(check(program({hat(), condition})));
    EXPECT_EQ(issue.rule, Rule::MissingCondition);
    EXPECT_EQ(issue.severity, Severity::Error);
    EXPECT_EQ(issue.blockId, "if");
}

TEST(Validator, InvalidNumber) {
    Block wait = block("wait", "control_wait");
    wait.inputs["SECONDS"].value = "deux";
    const Issue issue = single(check(program({hat(), wait})));
    EXPECT_EQ(issue.rule, Rule::InvalidNumber);
    EXPECT_NE(s2a::validation::describe(issue, Language::English).find("\"deux\""), std::string::npos);
    // Decimal commas are numbers.
    wait.inputs["SECONDS"].value = "0,5";
    EXPECT_TRUE(check(program({hat(), wait})).empty());
}

TEST(Validator, InvalidPin) {
    Block write = block("write", "pins_digital_write");
    write.fields = {{"PIN", "42"}, {"STATE", "HIGH"}};
    const Issue issue = single(check(program({hat(), write})));
    EXPECT_EQ(issue.rule, Rule::InvalidPin);
    EXPECT_NE(s2a::validation::describe(issue, Language::English).find("Arduino Uno"), std::string::npos);
    // The same pin exists on a Mega.
    Project mega = program({hat(), write});
    mega.boardId = "mega";
    EXPECT_TRUE(check(mega).empty());
}

TEST(Validator, PinCapability) {
    Block pwm = block("pwm", "pins_analog_write");
    pwm.fields["PIN"] = "7"; // no PWM on pin 7 of an Uno
    pwm.inputs["VALUE"].value = "128";
    const Issue issue = single(check(program({hat(), pwm})));
    EXPECT_EQ(issue.rule, Rule::PinCapability);
    const std::string text = s2a::validation::describe(issue, Language::English);
    EXPECT_NE(text.find("PWM"), std::string::npos) << text;
    EXPECT_NE(text.find("3, 5, 6, 9, 10, 11"), std::string::npos) << text;
}

TEST(Validator, MissingVariable) {
    Block set = block("set", "variables_set");
    set.fields["VARIABLE"] = "deleted";
    set.inputs["VALUE"].value = "1";
    const Issue issue = single(check(program({hat(), set})));
    EXPECT_EQ(issue.rule, Rule::MissingVariable);
}

TEST(Validator, UnknownBlock) {
    const Issue issue = single(check(program({hat(), block("future", "teleport_robot")})));
    EXPECT_EQ(issue.rule, Rule::UnknownBlock);
    EXPECT_NE(s2a::validation::describe(issue, Language::English).find("teleport_robot"), std::string::npos);
}

TEST(Validator, ChecksReportersAndNestedStacks) {
    Block sum = block("sum", "operators_add");
    sum.inputs["NUM1"].value = "x";
    Block wait = block("wait", "control_wait");
    wait.inputs["SECONDS"].block = sum;
    Block repeat = block("repeat", "control_repeat");
    repeat.inputs["TIMES"].value = "3";
    repeat.statements["SUBSTACK"] = {wait};
    const Issue issue = single(check(program({hat(), repeat})));
    EXPECT_EQ(issue.rule, Rule::InvalidNumber);
    EXPECT_EQ(issue.blockId, "sum");
}

TEST(Validator, EveryRuleHasMessagesInBothLanguages) {
    for (int rule = static_cast<int>(Rule::NoStartBlock); rule <= static_cast<int>(Rule::UnknownBlock); ++rule) {
        const std::string key(s2a::validation::ruleKey(static_cast<Rule>(rule)));
        for (const Language language : {Language::French, Language::English}) {
            EXPECT_NE(s2a::tr(key, language), key) << key;
            EXPECT_NE(s2a::tr(key + "_hint", language), key + "_hint") << key;
        }
    }
}

} // namespace

namespace {

TEST(Validator, PhoneBlocksNeedBluetooth) {
    Block send = block("send", "phone_send");
    send.inputs["VALUE"].value = "Bonjour";
    Project project = program({hat(), send});
    project.boardId = "esp8266";
    auto issues = check(project);
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, Rule::NoBluetooth);
    EXPECT_EQ(issues[0].blockId, "send");
    EXPECT_NE(s2a::validation::describe(issues[0], Language::English).find("no Bluetooth"), std::string::npos);
    project.boardId = "esp32";
    EXPECT_TRUE(check(project).empty());
    // An Arduino board talks to the phone through an HC-05 / HC-06 module.
    project.boardId = "uno";
    issues = check(project);
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, Rule::ModuleMissing);
    EXPECT_NE(s2a::validation::describe(issues[0], Language::English).find("Bluetooth module"), std::string::npos);
}

Block module(std::string id, const char* rx, const char* tx) {
    Block result = block(std::move(id), "phone_module");
    result.fields["RX_PIN"] = rx;
    result.fields["TX_PIN"] = tx;
    return result;
}

TEST(Validator, ChecksTheBluetoothModule) {
    Block send = block("send", "phone_send");
    send.inputs["VALUE"].value = "Bonjour";
    Project project = program({hat(), module("module", "10", "11"), send});
    project.boardId = "uno";
    EXPECT_TRUE(check(project).empty());
    // Not on the USB pins.
    project.scripts[0].blocks[1] = module("module", "0", "1");
    auto issues = check(project);
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, Rule::ModulePins);
    EXPECT_NE(s2a::validation::describe(issues[0], Language::English).find("USB"), std::string::npos);
    // A Mega receives with SoftwareSerial on some pins only, or uses its serial ports.
    project.boardId = "mega";
    project.scripts[0].blocks[1] = module("module", "2", "3");
    issues = check(project);
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, Rule::ModulePins);
    project.scripts[0].blocks[1] = module("module", "19", "18");
    EXPECT_TRUE(check(project).empty());
    // Two modules on different pins.
    project.scripts[0].blocks.insert(project.scripts[0].blocks.begin() + 2, module("again", "10", "11"));
    issues = check(project);
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, Rule::ModuleTwice);
    EXPECT_EQ(issues[0].blockId, "again");
    // The ESP32 has Bluetooth built in; the ESP8266 has none.
    project.scripts[0].blocks.erase(project.scripts[0].blocks.begin() + 2);
    project.boardId = "esp32";
    issues = check(project);
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, Rule::ModuleNotNeeded);
    project.boardId = "esp8266";
    issues = check(project);
    ASSERT_FALSE(issues.empty());
    EXPECT_EQ(issues[0].rule, Rule::NoBluetooth);
}

} // namespace

// Reported by the user: "set lampe to 0" was offered for the state of the advanced button example.
TEST(Validator, RefusesToSetAVariableThatMyCodeChangesItself) {
    Project project = program({block("start", "events_when_program_starts")});
    Block set = block("set", "mycode_set");
    set.fields["NAME"] = "lampe";
    set.inputs["VALUE"].value = "0";
    Block config = block("config", "mycode_set");
    config.fields["NAME"] = "speed";
    config.inputs["VALUE"].value = "5";
    project.scripts[0].blocks.push_back(set);
    project.scripts[0].blocks.push_back(config);
    project.userCode = UserCode{ProgramLanguage::Cpp,
                                "int lampe = 0;\nint speed = 2;\nvoid toggle() {\n  lampe = 1 - lampe;\n}\n"};
    const Issue issue = single(check(project));
    EXPECT_EQ(issue.rule, Rule::UserCodeOwned);
    EXPECT_EQ(issue.blockId, "set");
    EXPECT_NE(s2a::validation::describe(issue, Language::English).find("\"lampe\" is changed by a function"),
              std::string::npos);
}

// No example offers or uses a block that would disturb its own code, and every example is valid.
TEST(Validator, EveryExampleNamesTheArgumentsOfItsBlocksAsTheCatalog) {
    // A misnamed slot ("TEXT" for "VALUE") is silently empty: the block shows and runs 0.
    const auto& catalog = s2a::BlockCatalog::builtin();
    for (const auto& entry : std::filesystem::directory_iterator(S2A_EXAMPLES_DIR)) {
        const auto file = entry.path() / "project.json";
        if (!std::filesystem::exists(file)) {
            continue;
        }
        const auto project = load(file);
        ASSERT_TRUE(project.ok()) << file;
        forEachBlock(project.value(), [&](const Block& block) {
            const auto* definition = catalog.find(block.type);
            if (definition == nullptr || block.type.rfind("mycode_", 0) == 0) {
                return; // "My code" blocks have the slots of the user's functions
            }
            const auto check = [&](const std::string& name) {
                EXPECT_NE(definition->findArg(name), nullptr)
                    << entry.path().filename() << ": block " << block.id << " (" << block.type << ") has no \"" << name
                    << "\"";
            };
            for (const auto& [name, value] : block.fields) {
                check(name);
            }
            for (const auto& [name, input] : block.inputs) {
                check(name);
            }
            for (const auto& [name, stack] : block.statements) {
                check(name);
            }
        });
    }
}

TEST(Validator, EveryExampleIsValidAndOnlySetsFreeVariables) {
    for (const auto& entry : std::filesystem::directory_iterator(S2A_EXAMPLES_DIR)) {
        const auto file = entry.path() / "project.json";
        if (!std::filesystem::exists(file)) {
            continue;
        }
        const auto project = load(file);
        ASSERT_TRUE(project.ok()) << file;
        for (const Issue& issue : check(project.value())) {
            EXPECT_NE(issue.severity, Severity::Error)
                << entry.path().filename() << ": " << s2a::validation::describe(issue, Language::English);
        }
    }
}
