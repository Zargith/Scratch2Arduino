#include "s2a/convert.hpp"
#include "s2a/project.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace {

using s2a::convert::convert;
using s2a::project::ProgramLanguage;

constexpr auto kCpp = ProgramLanguage::Cpp;
constexpr auto kPython = ProgramLanguage::Python;
constexpr auto kJs = ProgramLanguage::JavaScript;

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

TEST(Convert, CppToPython) {
    const auto result = convert(R"(// Clignote
const int LED = 13;
int compteur = 0;

void clignoter(int fois) {
  for (int i = 0; i < fois; i++) {
    digitalWrite(LED, HIGH);
    delay(200);
  }
  compteur += fois;
  if (compteur > 10 && !(fois == 0)) {
    compteur = 0;
  } else if (compteur == 5) {
    Serial.println("cinq");
  } else {
    compteur++;
  }
}

float moitie(float x) {
  return x > 0 ? x / 2 : 0;
}
)",
                                kCpp, kPython);
    EXPECT_TRUE(result.toCheck.empty());
    const std::string& code = result.code;
    EXPECT_TRUE(has(code, "# Clignote\nLED = 13\ncompteur = 0\n\ndef clignoter(fois):\n    global compteur\n")) << code;
    EXPECT_TRUE(has(code, "    for i in range(fois):\n        digitalWrite(LED, HIGH)\n        delay(200)\n")) << code;
    EXPECT_TRUE(has(code, "    if compteur > 10 and not fois == 0:\n        compteur = 0\n    elif compteur == 5:\n"
                          "        print(\"cinq\")\n    else:\n        compteur += 1\n"))
        << code;
    EXPECT_TRUE(has(code, "def moitie(x):\n    return x / 2 if x > 0 else 0\n")) << code;
}

TEST(Convert, PythonToCppGuessesTheTypes) {
    const auto result = convert(R"(import time

vitesse = 0.5
nom = "robot"
actif = True

def attendre(secondes):
    time.sleep(secondes)

def doubler(n):
    return n * 2

def allumer(broche, fois):
    for i in range(1, fois + 1, 2):
        digitalWrite(broche, HIGH)
        print(i)
)",
                                kPython, kCpp);
    EXPECT_TRUE(result.toCheck.empty());
    const std::string& code = result.code;
    EXPECT_TRUE(has(code, "float vitesse = 0.5;\nString nom = \"robot\";\nbool actif = true;\n")) << code;
    EXPECT_TRUE(has(code, "void attendre(float secondes) {\n    delay(secondes * 1000);\n}")) << code;
    EXPECT_TRUE(has(code, "float doubler(float n) {\n    return n * 2;\n}")) << code;
    // Used as a pin and as a count: int.
    EXPECT_TRUE(has(code, "void allumer(int broche, int fois) {\n    for (int i = 1; i < fois + 1; i += 2) {\n")) << code;
    EXPECT_TRUE(has(code, "        Serial.println(i);\n")) << code;
}

TEST(Convert, JavaScriptToPythonAndBack) {
    const std::string js = R"(let total = 0;
const POIDS = [1, 2, 3];

function ajouter(valeur) {
    for (const p of [1, 2, 3]) {
        total += valeur * p;
    }
    while (total > 100) {
        total -= 100;
    }
    return Math.max(total, 0);
}
)";
    const auto python = convert(js, kJs, kPython);
    EXPECT_TRUE(python.toCheck.empty());
    EXPECT_TRUE(has(convert(js, kJs, kCpp).code, "const int POIDS[] = {1, 2, 3};")) << convert(js, kJs, kCpp).code;
    EXPECT_TRUE(has(python.code, "def ajouter(valeur):\n    global total\n    for p in (1, 2, 3):\n")) << python.code;
    EXPECT_TRUE(has(python.code, "    return max(total, 0)\n")) << python.code;
    const auto back = convert(python.code, kPython, kJs);
    EXPECT_TRUE(has(back.code, "for (const p of [1, 2, 3]) {")) << back.code;
    EXPECT_TRUE(has(back.code, "return Math.max(total, 0);")) << back.code;
}

TEST(Convert, KeepsParenthesesWhereTheyMatter) {
    const auto result = convert("x = (a + b) * c - (d - e)\ny = not (a and b)\nz = -(a + 1)\n", kPython, kCpp);
    EXPECT_TRUE(has(result.code, "x = (a + b) * c - (d - e);")) << result.code;
    EXPECT_TRUE(has(result.code, "y = !(a && b);")) << result.code;
    EXPECT_TRUE(has(result.code, "z = -(a + 1);")) << result.code;
    const auto python = convert("bool v = !a == b;\n", kCpp, kPython);
    EXPECT_TRUE(has(python.code, "v = (not a) == b")) << python.code;
}

TEST(Convert, KeepsWhatItDoesNotKnowAsAComment) {
    const auto result = convert("int notes[3] = {1, 2, 3};\nvoid f() {\n  g();\n}\n", kCpp, kPython);
    EXPECT_EQ(result.toCheck, std::vector<int>{1});
    EXPECT_TRUE(has(result.code, "# A convertir a la main / to convert by hand:\n# int notes[3] = {1, 2, 3};\n"))
        << result.code;
    EXPECT_TRUE(has(result.code, "def f():\n    g()\n")) << result.code;

    const auto python = convert("carre = lambda x: x * x\nprint(carre(2))\n", kPython, kJs);
    EXPECT_EQ(python.toCheck, std::vector<int>{1});
    EXPECT_TRUE(has(python.code, "print(carre(2));")) << python.code;

    // Half-typed code never stops it.
    EXPECT_NO_THROW((void)convert("void f( {\n  if (x\n", kCpp, kPython));
    EXPECT_NO_THROW((void)convert("def f(:\n    if\n", kPython, kCpp));
    EXPECT_NO_THROW((void)convert("}}}{{(", kJs, kCpp));
}

TEST(Convert, IncludesComeBackInCpp) {
    const auto python = convert("#include <EEPROM.h>\nint f() {\n  return EEPROM.read(0);\n}\n", kCpp, kPython);
    EXPECT_EQ(python.toCheck, std::vector<int>{1});
    EXPECT_TRUE(has(python.code, "# C++: #include <EEPROM.h>\n")) << python.code;
    const auto back = convert(python.code, kPython, kCpp);
    EXPECT_TRUE(has(back.code, "#include <EEPROM.h>\n")) << back.code;
    EXPECT_TRUE(has(back.code, "return EEPROM.read(0);")) << back.code;
}

TEST(Convert, EveryExampleRoundTripsWithoutManualWork) {
    for (const auto& entry : std::filesystem::directory_iterator(S2A_EXAMPLES_DIR)) {
        if (entry.path().filename().string().find("-advanced") == std::string::npos) {
            continue;
        }
        const auto project = s2a::project::load(entry.path() / "project.json");
        ASSERT_TRUE(project.ok()) << entry.path();
        ASSERT_TRUE(project.value().userCode.has_value()) << entry.path();
        const std::string& code = project.value().userCode->code;
        const ProgramLanguage from = project.value().userCode->language;
        for (const ProgramLanguage to : {kCpp, kPython, kJs}) {
            const auto there = convert(code, from, to);
            const auto back = convert(there.code, to, from);
            const bool include = code.find("#include") != std::string::npos;
            EXPECT_EQ(there.toCheck.empty(), !include || to == kCpp) << entry.path() << "\n" << there.code;
            EXPECT_FALSE(back.code.empty());
            // Every function survives.
            for (const char* name : {"feux", "angleLisse", "moyenne", "fahrenheit", "surveillerBouton", "record", "rouler", "sos"}) {
                if (code.find(std::string(name) + "(") != std::string::npos) {
                    EXPECT_TRUE(has(there.code, std::string(name) + "(")) << name << " in " << there.code;
                    EXPECT_TRUE(has(back.code, std::string(name) + "(")) << name << " in " << back.code;
                }
            }
        }
    }
}

TEST(Convert, KeepsACommentAboveTheFunctionItDescribes) {
    const auto js = convert("def a(x):\n    if x:\n        b()\n\n# About c\ndef c():\n    # inside c\n    d()\n", kPython, kJs);
    EXPECT_TRUE(has(js.code, "        b();\n    }\n}\n\n// About c\nfunction c() {\n    // inside c\n")) << js.code;
}

TEST(Convert, BlocksAreNotAText) {
    EXPECT_EQ(convert("x", ProgramLanguage::Blocks, kCpp).code, "x");
}

} // namespace
