#include "s2a/validator.hpp"

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
    for (const char* name : {"blink", "all-blocks"}) {
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
    EXPECT_NE(s2a::validation::describe(issue, Language::French).find("« répéter … fois » est vide"), std::string::npos)
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
