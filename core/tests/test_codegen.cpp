#include "s2a/arduino_cli.hpp"
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

TEST(Codegen, WritesToTheSerialMonitor) {
    Block text = block("t", "serial_print");
    text.inputs["VALUE"].value = "Il a dit \"salut\" \\ ok";
    Block analog = block("a", "pins_analog_read");
    analog.fields["PIN"] = "A0";
    Block value = block("v", "serial_print");
    value.inputs["VALUE"].block = analog;
    const auto sketch = generate(withScript({block("hat", "events_when_program_starts"), text, value}));
    EXPECT_NE(sketch.source.find("void setup() {\n  Serial.begin(9600);"), std::string::npos) << sketch.source;
    EXPECT_NE(sketch.source.find(R"(Serial.println(F("Il a dit \"salut\" \\ ok"));)"), std::string::npos)
        << sketch.source;
    EXPECT_NE(sketch.source.find("s2a_print(analogRead(A0));"), std::string::npos) << sketch.source;
    EXPECT_NE(sketch.source.find("void s2a_print(double value) {"), std::string::npos);
}

TEST(Codegen, OpensTheSerialPortOnlyWhenUsed) {
    const auto sketch = generate(withScript({block("hat", "events_when_program_starts")}));
    EXPECT_EQ(sketch.source.find("Serial"), std::string::npos) << sketch.source;
}

TEST(Codegen, EscapesStringLiterals) {
    EXPECT_EQ(s2a::codegen::stringLiteral("a\"b\\c\nd\te\rf\x01"), R"("a\"b\\c\nd\tef")");
    EXPECT_EQ(s2a::codegen::stringLiteral("été"), "\"été\"");
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

TEST(Codegen, DrivesComponents) {
    Block rgb = block("rgb", "lights_rgb_color");
    rgb.fields = {{"RED_PIN", "9"}, {"GREEN_PIN", "10"}, {"BLUE_PIN", "11"}, {"COLOR", "CYAN"}};
    Block servo = block("servo", "motors_servo_write");
    servo.fields["PIN"] = "3";
    servo.inputs["ANGLE"].value = "45";
    Block note = block("note", "sound_note");
    note.fields["NOTE"] = "392";
    Block play = block("play", "sound_play_tone");
    play.fields["PIN"] = "8";
    play.inputs["FREQUENCY"].block = note;
    play.inputs["SECONDS"].value = "0,25";
    Block motor = block("motor", "motors_dc_run");
    motor.fields = {{"EN_PIN", "6"}, {"IN1_PIN", "7"}, {"IN2_PIN", "4"}, {"DIRECTION", "BACKWARD"}};
    motor.inputs["SPEED"].value = "50";
    Block stop = block("stop", "motors_dc_stop");
    const auto source =
        generate(withScript({block("hat", "events_when_program_starts"), rgb, servo, play, motor, stop})).source;
    for (const char* expected :
         {"#include <Servo.h>\n\nServo s2a_servo_3;\n", "  s2a_servo_3.attach(3);\n", "analogWrite(9, 0);",
          "analogWrite(10, 255);", "analogWrite(11, 255);", "s2a_servo_3.write(constrain((int)(45), 0, 180));",
          "  s2a_tone(8, 392);\n  s2a_wait(0.25);\n  s2a_quiet(8);\n", "  pinMode(pin, INPUT);\n", "void s2a_tone(int pin, double frequency) {",
          "s2a_motor(6, 7, 4, -1, 50);", "s2a_motor(6, 7, 4, 0, 0);", "  pinMode(6, OUTPUT);"}) {
        EXPECT_NE(source.find(expected), std::string::npos) << expected << "\n" << source;
    }
    EXPECT_EQ(source.find("pinMode(3,"), std::string::npos) << "attach() sets the servo pin";
    EXPECT_EQ(source.find("pinMode(8,"), std::string::npos) << "tone() sets a buzzer pin";
}

TEST(Codegen, KeepsABuzzerPinThatOtherBlocksDrive) {
    Block play = block("play", "sound_play_tone");
    play.fields["PIN"] = "8";
    Block off = block("off", "pins_digital_write");
    off.fields = {{"PIN", "8"}, {"STATE", "LOW"}};
    const auto source = generate(withScript({block("hat", "events_when_program_starts"), play, off})).source;
    EXPECT_EQ(source.find("s2a_quiet"), std::string::npos) << source;
    EXPECT_NE(source.find("  noTone(8);\n  digitalWrite(8, LOW);"), std::string::npos) << source;
    EXPECT_NE(source.find("  pinMode(8, OUTPUT);"), std::string::npos) << source;
}

TEST(Codegen, ReadsSensors) {
    Block distance = block("distance", "sensors_distance");
    distance.fields = {{"TRIG_PIN", "12"}, {"ECHO_PIN", "11"}};
    Block print = block("print", "serial_print");
    print.inputs["VALUE"].block = distance;
    Block button = block("button", "sensors_button_pressed");
    button.fields["PIN"] = "2";
    Block until = block("until", "control_wait_until");
    until.inputs["CONDITION"].block = button;
    Block light = block("light", "sensors_light");
    light.fields["PIN"] = "A1";
    Block print2 = block("print2", "serial_print");
    print2.inputs["VALUE"].block = light;
    const auto source =
        generate(withScript({block("hat", "events_when_program_starts"), print, until, print2})).source;
    for (const char* expected :
         {"s2a_print(s2a_distance(12, 11));", "double s2a_distance(int trig, int echo) {", "pinMode(12, OUTPUT);",
          "pinMode(11, INPUT);", "pinMode(2, INPUT_PULLUP);", "while (!(digitalRead(2) == LOW)) {",
          "s2a_print((analogRead(A1) * 100.0 / 1023.0));"}) {
        EXPECT_NE(source.find(expected), std::string::npos) << expected << "\n" << source;
    }
}

Project withBlinkFunction() {
    Block call = block("call", "functions_call");
    call.fields["FUNCTION"] = "f";
    Block forever = block("loop", "control_forever");
    forever.statements["SUBSTACK"] = {call};
    Project project = withScript({block("hat", "events_when_program_starts"), forever});
    project.functions = {{"f", "clignoter vite"}};
    Block define = block("define", "functions_define");
    define.fields["FUNCTION"] = "f";
    project.scripts.push_back({300, 0, {define, led("on", "ON"), wait("w", "0.1"), led("off", "OFF"), wait("w2", "0.1")}});
    return project;
}

TEST(Codegen, WritesFunctionsAsCppFunctions) {
    const auto sketch = generate(withBlinkFunction());
    for (const char* expected : {"void s2a_function_clignoter_vite();\n", "void s2a_function_clignoter_vite() {\n"
                                 "  digitalWrite(LED_BUILTIN, HIGH);\n  s2a_wait(0.1);\n",
                                 "void loop() {\n  s2a_function_clignoter_vite();\n}"}) {
        EXPECT_NE(sketch.source.find(expected), std::string::npos) << expected << "\n" << sketch.source;
    }
    EXPECT_EQ(lines(sketch.source).at(sketch.blockLines.at("on") - 1), "  digitalWrite(LED_BUILTIN, HIGH);");
}

TEST(Codegen, WritesFunctionsInsideScriptsThatRunSideBySide) {
    Project project = withBlinkFunction();
    project.scripts.push_back({600, 0, {block("hat2", "events_when_program_starts"), led("on2", "ON")}});
    const auto source = generate(project).source;
    EXPECT_EQ(source.find("void s2a_function_"), std::string::npos) << source;
    EXPECT_NE(source.find("{ // clignoter vite\n      digitalWrite(LED_BUILTIN, HIGH);\n      S2A_WAIT(task, 0.1);"),
              std::string::npos)
        << source;
}

TEST(Codegen, UsesComponentLibraries) {
    Block temperature = block("t", "sensors_temperature");
    temperature.fields = {{"MODEL", "DHT22"}, {"PIN", "3"}};
    Block lcd = block("lcd", "display_lcd_print");
    lcd.fields["LINE"] = "2";
    lcd.inputs["TEXT"].block = temperature;
    Block humidity = block("h", "sensors_humidity");
    humidity.fields = {{"MODEL", "DHT11"}, {"PIN", "3"}}; // the first block gives the model
    Block oled = block("oled", "display_oled_print");
    oled.inputs["TEXT"].value = "Fête \"ici\"";
    Block oled2 = block("oled2", "display_oled_print");
    oled2.fields["LINE"] = "3";
    oled2.inputs["TEXT"].block = humidity;
    Block fill = block("fill", "lights_neopixel_fill");
    fill.fields = {{"PIN", "5"}, {"COLOR", "YELLOW"}};
    fill.inputs["COUNT"].value = "12";
    Block one = block("one", "lights_neopixel_set");
    one.fields["PIN"] = "5"; // default count (8): the strip keeps the longest one
    Block show = block("show", "lights_matrix_show");
    show.fields["IMAGE"] = "HEART";
    Block dot = block("dot", "lights_matrix_dot");
    const auto source = generate(withScript({block("hat", "events_when_program_starts"), lcd, oled, oled2, fill, one,
                                             show, dot, block("clear", "display_lcd_clear")}))
                            .source;
    for (const char* expected :
         {"#include <Adafruit_NeoPixel.h>\n#include <DHT.h>\n#include <LiquidCrystal_I2C.h>\n#include <U8x8lib.h>\n"
          "#include <Wire.h>\n",
          "DHT s2a_dht_3(3, DHT22);", "  s2a_dht_3.begin();", "s2a_lcd_line(2, s2a_text(s2a_dht_read(s2a_dht_3, s2a_dht_3_last, false)));",
          "s2a_oled_line(1, F(\"Fete \\\"ici\\\"\"));", "s2a_oled_line(3, s2a_text(s2a_dht_read(s2a_dht_3, s2a_dht_3_last, true)));",
          "  s2a_lcd_begin();", "  s2a_oled.begin();\n  s2a_oled.setFont(u8x8_font_chroma48medium8_r);",
          "Adafruit_NeoPixel s2a_strip_5(12, 5, NEO_GRB + NEO_KHZ800);", "  s2a_strip_5.begin();",
          "s2a_strip_5.fill(s2a_strip_5.Color(255, 160, 0));", "s2a_pixel(s2a_strip_5, 1, s2a_strip_5.Color(255, 0, 0));",
          "byte s2a_matrix_12_11_10[8];", "  s2a_matrix_settings(din, clk, cs);\n  for (byte row = 0; row < 8; row++) {", "  s2a_matrix_init(12, 11, 10);",
          "static const byte image[8] = {0x00, 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18};",
          "s2a_matrix_image(12, 11, 10, s2a_matrix_12_11_10, image);",
          "s2a_matrix_dot(12, 11, 10, s2a_matrix_12_11_10, 1, 1, true);", "s2a_lcd->clear();"}) {
        EXPECT_NE(source.find(expected), std::string::npos) << expected << "\n" << source;
    }
    EXPECT_EQ(source.find("DHT11"), std::string::npos);
    EXPECT_EQ(source.find("pinMode(3,"), std::string::npos) << "the DHT library sets its pin";
    EXPECT_EQ(source.find("pinMode(5,"), std::string::npos) << "the NeoPixel library sets its pin";
}

TEST(Codegen, Drives16PinLcdScreens) {
    Block print = block("print", "display_lcd16_print");
    print.fields["LINE"] = "2";
    print.inputs["TEXT"].value = "Élève";
    Block other = block("other", "display_lcd16_print");
    other.fields = {{"RS_PIN", "2"}, {"E_PIN", "3"}, {"D4_PIN", "4"}, {"D5_PIN", "5"}, {"D6_PIN", "6"}, {"D7_PIN", "A0"}};
    Block clear = block("clear", "display_lcd16_clear");
    Block led = block("led", "pins_digital_write");
    led.fields["PIN"] = "13";
    const auto sketch = generate(withScript({block("hat", "events_when_program_starts"), print, other, clear, led}));
    const std::string& source = sketch.source;
    for (const char* expected :
         {"#include <LiquidCrystal.h>\n", "LiquidCrystal s2a_lcd16_12_11_10_9_8_7(12, 11, 10, 9, 8, 7);",
          "LiquidCrystal s2a_lcd16_2_3_4_5_6_A0(2, 3, 4, 5, 6, A0);", "  s2a_lcd16_12_11_10_9_8_7.begin(16, 2);",
          "  s2a_lcd16_2_3_4_5_6_A0.begin(16, 2);", "void s2a_lcd16_line(LiquidCrystal& lcd, int row, const String& text) {",
          "s2a_lcd16_line(s2a_lcd16_12_11_10_9_8_7, 2, F(\"Eleve\"));",
          "s2a_lcd16_line(s2a_lcd16_2_3_4_5_6_A0, 1, F(\"Bonjour !\"));", "s2a_lcd16_12_11_10_9_8_7.clear();",
          "  pinMode(13, OUTPUT);"}) {
        EXPECT_NE(source.find(expected), std::string::npos) << expected << "\n" << source;
    }
    EXPECT_EQ(source.find("pinMode(12,"), std::string::npos) << "the LiquidCrystal library sets its pins";
    EXPECT_EQ(source.find("LiquidCrystal s2a_lcd16_12_11_10_9_8_7", source.find("LiquidCrystal s2a_lcd16_12_11_10_9_8_7") + 1),
              std::string::npos)
        << "one object per wiring";
    EXPECT_EQ(s2a::toolchain::requiredLibraries(source), (std::vector<std::string>{"LiquidCrystal"}));
}

TEST(Codegen, AdaptsToTheEsp32) {
    Block servo = block("servo", "motors_servo_write");
    servo.fields["PIN"] = "13";
    Block knob = block("knob", "sensors_potentiometer");
    knob.fields["PIN"] = "34";
    Block print = block("print", "serial_print");
    print.inputs["VALUE"].block = knob;
    const auto source =
        generate(withScript({block("hat", "events_when_program_starts"), led("on", "ON"), servo, print}, "esp32")).source;
    for (const char* expected : {"#include <ESP32Servo.h>\n", "  pinMode(2, OUTPUT);", "digitalWrite(2, HIGH);",
                                 "  analogReadResolution(10);", "s2a_servo_13.attach(13);", "analogRead(34)"}) {
        EXPECT_NE(source.find(expected), std::string::npos) << expected << "\n" << source;
    }
    EXPECT_EQ(source.find("LED_BUILTIN"), std::string::npos) << source;
    EXPECT_EQ(source.find("<Servo.h>"), std::string::npos) << source;
    EXPECT_EQ(s2a::toolchain::requiredLibraries(source), (std::vector<std::string>{"ESP32Servo"}));
    // Arduino boards keep LED_BUILTIN and need no resolution change.
    const auto uno = generate(withScript({block("hat", "events_when_program_starts"), led("on", "ON")})).source;
    EXPECT_NE(uno.find("digitalWrite(LED_BUILTIN, HIGH);"), std::string::npos);
    EXPECT_EQ(uno.find("analogReadResolution"), std::string::npos);
}

TEST(Codegen, AsciiText) {
    EXPECT_EQ(s2a::codegen::asciiText("Élève à l'œil ½ 😀"), "Eleve a l'oeil ? ?");
    EXPECT_EQ(s2a::codegen::asciiText("a\tb\nc"), "abc");
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
