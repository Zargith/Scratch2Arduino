#include "s2a/usercode.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using s2a::project::ProgramLanguage;
using s2a::usercode::Declaration;
using s2a::usercode::declarations;
using Kind = Declaration::Kind;

/// "f(a,b)=" for a function with a value, "f(a)" without, "v" for a variable, "v!" read-only.
std::vector<std::string> summary(ProgramLanguage language, const std::string& code) {
    std::vector<std::string> result;
    for (const Declaration& declaration : declarations(language, code)) {
        std::string text = declaration.name;
        if (declaration.kind == Kind::Function) {
            text += "(";
            for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
                text += (i > 0 ? "," : "") + declaration.parameters[i];
            }
            text += declaration.returnsValue ? ")=" : ")";
        } else if (declaration.readOnly) {
            text += "!";
        } else if (declaration.changedByCode) {
            text += "~"; // the code's own state: read only from blocks
        }
        result.push_back(text + "@" + std::to_string(declaration.line));
    }
    return result;
}

TEST(UserCode, FindsCppFunctionsAndGlobals) {
    const std::string code = R"cpp(#include <Servo.h>
#define PIN 9
// int commented = 1;
int score = 0, lives = 3;
const float ratio = 1.5;
volatile unsigned long ticks;
Servo arm; /* not a number */
int table[4];
const char* name = "a; void hidden() {";

void blink(int times, unsigned long pause) {
  for (int i = 0; i < times; i++) { digitalWrite(13, HIGH); }
}

static float average(float a, float b) { return (a + b) / 2; }
void setup() {}
void loop() {}
void withServo(Servo s) {}
int five(int a, int b, int c, int d, int e) { return 5; }
long seconds(void)
{
  return millis() / 1000;
}
)cpp";
    EXPECT_EQ(summary(ProgramLanguage::Cpp, code),
              (std::vector<std::string>{"score@4", "lives@4", "ratio!@5", "ticks@6", "blink(times,pause)@11",
                                        "average(a,b)=@15", "seconds()=@20"}));
}

// Reported by the user: String variables and names starting with "_" gave no block.
TEST(UserCode, FindsStringVariablesAndNamesStartingWithAnUnderscore) {
    const std::string code = R"cpp(String str = "";
float _f;
int __reserved = 1;

void incrment_str(float _str) {
    str += _str;
}

String hello() { return "hi"; }
)cpp";
    EXPECT_EQ(summary(ProgramLanguage::Cpp, code),
              (std::vector<std::string>{"str~@1", "_f@2", "incrment_str(_str)@5", "hello()=@9"}));
}

TEST(UserCode, FindsPythonFunctionsAndGlobals) {
    const std::string code = R"py("""Mes fonctions.
def hidden():
"""
speed = 120
LIMIT: int = 10
same == 3

def blink(times, pause=300):
    for _ in range(times):
        digitalWrite(13, HIGH)

def average(a, b):
    # mean
    return (a + b) / 2

def rest(*values):
    pass
square = lambda x: x * x
    indented = 1
)py";
    EXPECT_EQ(summary(ProgramLanguage::Python, code),
              (std::vector<std::string>{"speed@4", "LIMIT@5", "blink(times,pause)@8", "average(a,b)=@12"}));
}

TEST(UserCode, FindsJavaScriptFunctionsAndGlobals) {
    const std::string code = R"js(// mon code
let speed = 120
const LIMIT = 10;
var a = 1, b = { x: 2 };
const twice = (x) => 2 * x;
const text = "function hidden() {";

function blink(times, pause = 300) {
  for (let i = 0; i < times; i++) { digitalWrite(13, HIGH); }
}

function average(a, b) {
  return (a + b) / 2;
}
function rest(...values) {}
)js";
    EXPECT_EQ(summary(ProgramLanguage::JavaScript, code),
              (std::vector<std::string>{"speed@2", "LIMIT!@3", "a@4", "b@4", "text!@6", "blink(times,pause)@8",
                                        "average(a,b)=@12"}));
}

TEST(UserCode, NeverFailsOnHalfTypedCode) {
    for (const auto language : {ProgramLanguage::Cpp, ProgramLanguage::Python, ProgramLanguage::JavaScript}) {
        for (const std::string code : {"", "void f(", "int x = ", "def f(a,\n", "function f(a) { return", "\"unterminated",
                                       "/* open comment", "}}}{{{", "''' open"}) {
            EXPECT_NO_THROW((void)declarations(language, code)) << code;
        }
    }
    EXPECT_TRUE(declarations(ProgramLanguage::Blocks, "int x;").empty());
}

TEST(UserCode, FindsDeclarationsOfAProjectAndItsBlocks) {
    s2a::project::Project project;
    EXPECT_TRUE(s2a::usercode::declarations(project).empty());
    project.userCode = s2a::project::UserCode{ProgramLanguage::Python, "x = 1\n"};
    const auto found = s2a::usercode::declarations(project);
    ASSERT_EQ(found.size(), 1U);
    EXPECT_NE(s2a::usercode::find(found, "x", Kind::Variable), nullptr);
    EXPECT_EQ(s2a::usercode::find(found, "x", Kind::Function), nullptr);
    EXPECT_FALSE(s2a::usercode::usesUserCode(project));
    s2a::project::Block call;
    call.id = "c";
    call.type = "mycode_call";
    project.scripts.push_back({0, 0, {call}});
    EXPECT_TRUE(s2a::usercode::usesUserCode(project));
}

} // namespace

// The code's own state (changed by its functions) is read-only for blocks, in every language;
// comparisons do not count as changes.
TEST(UserCode, MarksTheVariablesTheCodeChangesItself) {
    EXPECT_EQ(summary(ProgramLanguage::Cpp, "int lampe = 0;\nint avant = 1;\nint seuil = 3;\nint compte;\n"
                                            "void f() {\n  if (seuil == 2 || seuil <= 1 || seuil != 0) { lampe = 1 - lampe; }\n"
                                            "  avant += 2;\n  ++compte;\n}\n"),
              (std::vector<std::string>{"lampe~@1", "avant~@2", "seuil@3", "compte~@4", "f()@5"}));
    EXPECT_EQ(summary(ProgramLanguage::Python, "lampe = 0\nvitesse = 2\n\ndef basculer():\n    global lampe\n"
                                               "    if vitesse == 2:\n        lampe = 1 - lampe\n"),
              (std::vector<std::string>{"lampe~@1", "vitesse@2", "basculer()@4"}));
    EXPECT_EQ(summary(ProgramLanguage::JavaScript, "let lampe = 0;\nlet vitesse = 2;\nfunction basculer() {\n"
                                                   "  if (vitesse === 2) { lampe = 1 - lampe; }\n}\n"),
              (std::vector<std::string>{"lampe~@1", "vitesse@2", "basculer()@3"}));
}
