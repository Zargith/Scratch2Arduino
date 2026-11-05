#include "s2a/codegen.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

namespace s2a::codegen {

namespace {

using project::Block;
using project::Project;
using project::Stack;

constexpr std::string_view kIndent = "  ";

enum class PinMode { Input, InputPullup, Output };

std::string formatNumber(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(15) << value;
    return stream.str();
}

std::string oneLine(std::string text) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    std::replace(text.begin(), text.end(), '\r', ' ');
    return text;
}

/// Lines of one function body, with the index of the first line of each block.
struct Body {
    std::vector<std::string> lines;
    std::map<std::string, int> blockLines;

    void add(int depth, const std::string& text) {
        std::string line;
        for (int i = 0; i < depth; ++i) {
            line += kIndent;
        }
        lines.push_back(line + text);
    }

    void mark(const std::string& blockId) {
        if (!blockId.empty()) {
            blockLines.emplace(blockId, static_cast<int>(lines.size()));
        }
    }
};

class Generator {
public:
    Generator(const Project& project, const BlockCatalog& catalog, const Board* board)
        : project_(project), catalog_(catalog), board_(board) {
        std::set<std::string> used;
        for (const project::Variable& variable : project.variables) {
            const std::string base = "var_" + identifier(variable.name);
            std::string name = base;
            for (int suffix = 2; used.count(name) != 0; ++suffix) {
                name = base + "_" + std::to_string(suffix);
            }
            used.insert(name);
            variables_[variable.id] = name;
        }
        for (const project::Function& function : project.functions) {
            const std::string base = "s2a_function_" + identifier(function.name);
            std::string name = base;
            for (int suffix = 2; used.count(name) != 0; ++suffix) {
                name = base + "_" + std::to_string(suffix);
            }
            used.insert(name);
            functions_[function.id] = name;
        }
        for (const project::Script& script : project.scripts) {
            if (!script.blocks.empty() && script.blocks.front().type == kDefine) {
                definitions_.emplace(field(script.blocks.front(), "FUNCTION"), &script);
            }
        }
    }

    Sketch run() {
        std::size_t runnable = 0;
        for (const project::Script& script : project_.scripts) {
            runnable += !script.blocks.empty() && isStart(script.blocks.front()) ? 1 : 0;
        }
        // Several scripts must run side by side, as in Scratch: each becomes a task that gives
        // way at every wait and every loop turn. A single script keeps the classic Arduino form.
        tasks_ = runnable > 1;
        for (const project::Script& script : project_.scripts) {
            if (!script.blocks.empty() && script.blocks.front().type == kDefine && !tasks_) {
                function(script);
                continue;
            }
            if (script.blocks.empty() || !isStart(script.blocks.front())) {
                continue; // Scratch rule: loose blocks do not run; functions run when called.
            }
            if (tasks_) {
                Body task;
                task.mark(script.blocks.front().id);
                for (std::size_t i = 1; i < script.blocks.size(); ++i) {
                    statement(task, script.blocks[i], 1);
                }
                tasks_bodies_.push_back(std::move(task));
                continue;
            }
            setup_.mark(script.blocks.front().id);
            const std::size_t last = script.blocks.size() - 1;
            for (std::size_t i = 1; i < script.blocks.size(); ++i) {
                const Block& block = script.blocks[i];
                if (i == last && block.type == "control_forever") {
                    loop_.mark(block.id);
                    statements(loop_, block, "SUBSTACK", 1);
                } else {
                    statement(setup_, block, 1);
                }
            }
        }
        return assemble();
    }

private:
    static constexpr std::string_view kDefine = "functions_define";

    /// A hat that starts a script when the program starts (not a function definition).
    bool isStart(const Block& block) const {
        const BlockDefinition* definition = catalog_.find(block.type);
        return definition != nullptr && definition->shape == BlockShape::Hat && block.type != kDefine;
    }

    /// A "define" script as a C++ function (single-script programs).
    void function(const project::Script& script) {
        const auto name = functions_.find(field(script.blocks.front(), "FUNCTION"));
        if (name == functions_.end() || generatedFunctions_.count(name->second) != 0) {
            return; // the validator reports a missing or duplicated function
        }
        generatedFunctions_.insert(name->second);
        Body body;
        body.mark(script.blocks.front().id);
        for (std::size_t i = 1; i < script.blocks.size(); ++i) {
            statement(body, script.blocks[i], 1);
        }
        functionBodies_.emplace_back(name->second, std::move(body));
    }

    // --- arguments -------------------------------------------------------------------

    std::string defaultValue(const Block& block, std::string_view name) const {
        const BlockDefinition* definition = catalog_.find(block.type);
        const BlockArg* arg = definition != nullptr ? definition->findArg(name) : nullptr;
        return arg != nullptr ? arg->defaultValue : std::string{};
    }

    std::string field(const Block& block, const std::string& name) const {
        auto found = block.fields.find(name);
        if (found != block.fields.end() && !found->second.empty()) {
            return found->second;
        }
        return defaultValue(block, name);
    }

    std::string number(const Block& block, const std::string& name) {
        auto found = block.inputs.find(name);
        if (found == block.inputs.end()) {
            return numberLiteral(defaultValue(block, name));
        }
        if (found->second.block) {
            return expression(*found->second.block);
        }
        return numberLiteral(found->second.value);
    }

    std::string condition(const Block& block, const std::string& name) {
        auto found = block.inputs.find(name);
        if (found != block.inputs.end() && found->second.block) {
            return expression(*found->second.block);
        }
        return "false";
    }

    /// Pin chosen in a menu, as written in the sketch ("13", "A0"); the default when invalid.
    std::string pinLabel(const Block& block, const std::string& name) const {
        std::string label = field(block, name);
        if (const Pin* found = board_ != nullptr ? board_->findPin(label) : nullptr) {
            label = found->label;
        }
        const bool valid = !label.empty() && std::all_of(label.begin(), label.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0;
        });
        if (!valid) {
            label = defaultValue(block, name);
            if (label.empty()) {
                label = "13";
            }
        }
        return label;
    }

    std::string pin(const Block& block, const std::string& name, PinMode mode) {
        const std::string label = pinLabel(block, name);
        // A pin used as an output anywhere stays an output; a pin set as an input with
        // pull-up keeps it when it is read.
        auto [it, inserted] = pinModes_.emplace(label, mode);
        if (!inserted && mode != PinMode::Input && it->second != PinMode::Output) {
            it->second = mode;
        }
        return label;
    }

    std::string variable(const Block& block) const {
        auto found = variables_.find(field(block, "VARIABLE"));
        return found != variables_.end() ? found->second : std::string{};
    }

    // --- expressions -----------------------------------------------------------------

    std::string binary(const Block& block, const char* op) {
        return "(" + number(block, "NUM1") + " " + op + " " + number(block, "NUM2") + ")";
    }

    std::string logic(const Block& block, const char* op) {
        return "(" + condition(block, "OPERAND1") + " " + op + " " + condition(block, "OPERAND2") + ")";
    }

    std::string expression(const Block& block) {
        static const std::map<std::string, const char*, std::less<>> kBinary = {
            {"operators_add", "+"}, {"operators_subtract", "-"}, {"operators_multiply", "*"},
            {"operators_lt", "<"},     {"operators_gt", ">"},
            {"operators_equals", "=="},
        };
        const std::string& type = block.type;
        if (auto op = kBinary.find(type); op != kBinary.end()) {
            return binary(block, op->second);
        }
        if (type == "operators_divide") { // 1 / 2 is 0.5, as in Scratch, not 0
            return "((double)" + number(block, "NUM1") + " / " + number(block, "NUM2") + ")";
        }
        if (type == "operators_and") {
            return logic(block, "&&");
        }
        if (type == "operators_or") {
            return logic(block, "||");
        }
        if (type == "operators_not") {
            return "(!" + condition(block, "OPERAND") + ")";
        }
        if (type == "operators_random") {
            helpers_.insert("random");
            return "s2a_random(" + number(block, "FROM") + ", " + number(block, "TO") + ")";
        }
        if (type == "operators_map") {
            helpers_.insert("map");
            return "s2a_map(" + number(block, "VALUE") + ", " + number(block, "FROM_LOW") + ", " +
                   number(block, "FROM_HIGH") + ", " + number(block, "TO_LOW") + ", " +
                   number(block, "TO_HIGH") + ")";
        }
        if (type == "pins_digital_read") {
            return "(digitalRead(" + pin(block, "PIN", PinMode::Input) + ") == " +
                   (field(block, "STATE") == "LOW" ? "LOW" : "HIGH") + ")";
        }
        if (type == "pins_analog_read") {
            return "analogRead(" + pin(block, "PIN", PinMode::Input) + ")";
        }
        if (type == "sensors_button_pressed") {
            return "(digitalRead(" + pin(block, "PIN", PinMode::InputPullup) + ") == LOW)";
        }
        if (type == "sensors_potentiometer" || type == "sensors_light") {
            return "(analogRead(" + pin(block, "PIN", PinMode::Input) + ") * 100.0 / 1023.0)";
        }
        if (type == "sensors_distance") {
            helpers_.insert("distance");
            const std::string trig = pin(block, "TRIG_PIN", PinMode::Output);
            return "s2a_distance(" + trig + ", " + pin(block, "ECHO_PIN", PinMode::Input) + ")";
        }
        if (type == "sound_note") {
            return numberLiteral(field(block, "NOTE"));
        }
        if (type == "variables_get") {
            const std::string name = variable(block);
            return name.empty() ? "0" : name;
        }
        const BlockDefinition* definition = catalog_.find(type);
        return definition != nullptr && definition->shape == BlockShape::Boolean ? "false" : "0";
    }

    std::string functionName(const std::string& id) const {
        for (const project::Function& function : project_.functions) {
            if (function.id == id) {
                return function.name;
            }
        }
        return id;
    }

    // --- statements ------------------------------------------------------------------

    void wait(Body& body, const std::string& seconds, int depth) {
        if (tasks_) {
            body.add(depth, "S2A_WAIT(task, " + seconds + ");");
        } else {
            helpers_.insert("wait");
            body.add(depth, "s2a_wait(" + seconds + ");");
        }
    }

    void statements(Body& body, const Block& parent, const std::string& name, int depth) {
        auto found = parent.statements.find(name);
        if (found == parent.statements.end()) {
            return;
        }
        for (const Block& block : found->second) {
            statement(body, block, depth);
        }
    }

    static const char* modeName(PinMode mode) {
        switch (mode) {
        case PinMode::Output: return "OUTPUT";
        case PinMode::InputPullup: return "INPUT_PULLUP";
        case PinMode::Input: break;
        }
        return "INPUT";
    }

    static std::string parenthesized(const std::string& condition) {
        if (condition.size() >= 2 && condition.front() == '(' && condition.back() == ')') {
            return condition;
        }
        return "(" + condition + ")";
    }

    void statement(Body& body, const Block& block, int depth) {
        body.mark(block.id);
        const std::string& type = block.type;
        if (type == "pins_builtin_led") {
            pinModes_["LED_BUILTIN"] = PinMode::Output;
            body.add(depth, std::string("digitalWrite(LED_BUILTIN, ") +
                                (field(block, "STATE") == "OFF" ? "LOW" : "HIGH") + ");");
        } else if (type == "pins_digital_write") {
            const std::string target = pin(block, "PIN", PinMode::Output);
            body.add(depth, "digitalWrite(" + target + ", " +
                                (field(block, "STATE") == "LOW" ? "LOW" : "HIGH") + ");");
        } else if (type == "pins_set_mode") {
            const PinMode mode = field(block, "MODE") == "INPUT" ? PinMode::Input : PinMode::InputPullup;
            const std::string target = pin(block, "PIN", mode);
            if (PinMode& declared = pinModes_[target]; declared != PinMode::Output) {
                declared = mode; // also in setup(), so that the pin is ready before it is read
            }
            body.add(depth, "pinMode(" + target + ", " + modeName(mode) + ");");
        } else if (type == "serial_print") {
            helpers_.insert("serial");
            const auto input = block.inputs.find("VALUE");
            if (input != block.inputs.end() && input->second.block) {
                helpers_.insert("print");
                body.add(depth, "s2a_print(" + expression(*input->second.block) + ");");
            } else {
                const std::string text = input != block.inputs.end() ? input->second.value : defaultValue(block, "VALUE");
                body.add(depth, text.empty() ? std::string("Serial.println();")
                                             : "Serial.println(F(" + stringLiteral(text) + "));");
            }
        } else if (type == "pins_analog_write") {
            const std::string target = pin(block, "PIN", PinMode::Output);
            body.add(depth, "analogWrite(" + target + ", constrain((int)(" + number(block, "VALUE") +
                                "), 0, 255));");
        } else if (type == "control_wait") {
            wait(body, number(block, "SECONDS"), depth);
        } else if (type == "lights_rgb_color" || type == "lights_rgb_mix") {
            std::array<std::string, 3> levels;
            if (type == "lights_rgb_mix") {
                levels = {"constrain((int)(" + number(block, "RED") + "), 0, 255)",
                          "constrain((int)(" + number(block, "GREEN") + "), 0, 255)",
                          "constrain((int)(" + number(block, "BLUE") + "), 0, 255)"};
            } else {
                const auto rgb = colourLevels(field(block, "COLOR"));
                levels = {std::to_string(rgb[0]), std::to_string(rgb[1]), std::to_string(rgb[2])};
            }
            const char* const pins[] = {"RED_PIN", "GREEN_PIN", "BLUE_PIN"};
            for (std::size_t i = 0; i < levels.size(); ++i) {
                body.add(depth, "analogWrite(" + pin(block, pins[i], PinMode::Output) + ", " + levels[i] + ");");
            }
        } else if (type == "sound_play_tone" || type == "sound_start_tone") {
            helpers_.insert("tone");
            const std::string target = pin(block, "PIN", PinMode::Output);
            body.add(depth, "s2a_tone(" + target + ", " + number(block, "FREQUENCY") + ");");
            if (type == "sound_play_tone") {
                wait(body, number(block, "SECONDS"), depth);
                body.add(depth, "noTone(" + target + ");");
            }
        } else if (type == "sound_stop") {
            body.add(depth, "noTone(" + pin(block, "PIN", PinMode::Output) + ");");
        } else if (type == "motors_servo_write") {
            const std::string servo = "s2a_servo_" + pinLabel(block, "PIN");
            servos_.insert(pinLabel(block, "PIN"));
            body.add(depth, servo + ".write(constrain((int)(" + number(block, "ANGLE") + "), 0, 180));");
        } else if (type == "motors_dc_run" || type == "motors_dc_stop") {
            helpers_.insert("motor");
            const std::string en = pin(block, "EN_PIN", PinMode::Output);
            const std::string in1 = pin(block, "IN1_PIN", PinMode::Output);
            const std::string in2 = pin(block, "IN2_PIN", PinMode::Output);
            const bool run = type == "motors_dc_run";
            const char* direction = !run ? "0" : (field(block, "DIRECTION") == "BACKWARD" ? "-1" : "1");
            body.add(depth, "s2a_motor(" + en + ", " + in1 + ", " + in2 + ", " + direction + ", " +
                                (run ? number(block, "SPEED") : std::string("0")) + ");");
        } else if (type == "control_wait_until") {
            if (tasks_) {
                body.add(depth, "S2A_WAIT_UNTIL(task, " + condition(block, "CONDITION") + ");");
            } else {
                body.add(depth, "while (!" + parenthesized(condition(block, "CONDITION")) + ") {");
                body.add(depth + 1, "delay(1);");
                body.add(depth, "}");
            }
        } else if (type == "control_repeat") {
            // In a task the counter must outlive the function, which returns at each wait.
            const std::string counter = (tasks_ ? "s2a_i" : "i") + std::to_string(++counters_);
            body.add(depth, std::string("for (") + (tasks_ ? "" : "long ") + counter + " = 0; " + counter +
                                " < (long)(" + number(block, "TIMES") + "); ++" + counter + ") {");
            statements(body, block, "SUBSTACK", depth + 1);
            if (tasks_) {
                body.add(depth + 1, "S2A_YIELD(task);");
            }
            body.add(depth, "}");
        } else if (type == "control_forever") {
            body.add(depth, "for (;;) {");
            statements(body, block, "SUBSTACK", depth + 1);
            body.add(depth + 1, tasks_ ? "S2A_YIELD(task);" : "yield();");
            body.add(depth, "}");
        } else if (type == "control_if" || type == "control_if_else") {
            body.add(depth, "if " + parenthesized(condition(block, "CONDITION")) + " {");
            statements(body, block, "SUBSTACK", depth + 1);
            if (type == "control_if_else") {
                body.add(depth, "} else {");
                statements(body, block, "SUBSTACK2", depth + 1);
            }
            body.add(depth, "}");
        } else if (type == "functions_call") {
            const std::string id = field(block, "FUNCTION");
            const auto name = functions_.find(id);
            const auto definition = definitions_.find(id);
            if (name == functions_.end() || definition == definitions_.end()) {
                return; // reported by the validator
            }
            if (!tasks_) {
                body.add(depth, name->second + "();");
            } else if (inlining_.insert(id).second) {
                // Side by side, a wait inside the function must let the other scripts run:
                // its blocks are written here, inside the script.
                body.add(depth, "{ // " + oneLine(functionName(id)));
                const project::Stack& stack = definition->second->blocks;
                for (std::size_t i = 1; i < stack.size(); ++i) {
                    statement(body, stack[i], depth + 1);
                }
                body.add(depth, "}");
                inlining_.erase(id);
            }
        } else if (type == "variables_set" || type == "variables_change") {
            const std::string name = variable(block);
            if (!name.empty()) {
                body.add(depth, name + (type == "variables_set" ? " = " : " += ") +
                                    number(block, "VALUE") + ";");
            }
        }
        // Unknown blocks generate nothing: the validator reports them.
    }

    // --- assembly --------------------------------------------------------------------

    void helper(std::vector<std::string>& out, const char* name,
                std::initializer_list<const char*> lines) const {
        if (helpers_.count(name) != 0) {
            out.insert(out.end(), lines.begin(), lines.end());
            out.emplace_back("");
        }
    }

    Sketch assemble() const {
        std::vector<std::string> out;
        out.push_back("// Scratch2Arduino: " + oneLine(project_.name));
        if (board_ != nullptr) {
            out.push_back("// " + board_->name);
        }
        out.emplace_back("");
        if (!servos_.empty()) {
            out.emplace_back("#include <Servo.h>");
            out.emplace_back("");
            for (const std::string& label : servos_) {
                out.push_back("Servo s2a_servo_" + label + ";");
            }
            out.emplace_back("");
        }

        for (const project::Variable& variable : project_.variables) {
            out.push_back("double " + variables_.at(variable.id) + " = " +
                          numberLiteral(formatNumber(variable.initialValue)) + "; // " +
                          oneLine(variable.name));
        }
        if (!project_.variables.empty()) {
            out.emplace_back("");
        }

        helper(out, "print",
               {"void s2a_print(double value) {",
                "  if (value == floor(value) && fabs(value) < 2000000000.0) {",
                "    Serial.println((long)value);", "  } else {", "    Serial.println(value, 2);", "  }", "}"});
        helper(out, "wait",
               {"void s2a_wait(double seconds) {", "  if (seconds > 0) {",
                "    delay((unsigned long)(seconds * 1000.0));", "  }", "}"});
        helper(out, "tone",
               {"void s2a_tone(int pin, double frequency) {", "  if (frequency < 31) {", "    noTone(pin);",
                "  } else {", "    tone(pin, (unsigned int)min(frequency, 65535.0));", "  }", "}"});
        helper(out, "distance",
               {"// HC-SR04: the echo lasts 58 microseconds per centimetre; 400 cm when nothing answers.",
                "double s2a_distance(int trig, int echo) {", "  digitalWrite(trig, LOW);",
                "  delayMicroseconds(2);", "  digitalWrite(trig, HIGH);", "  delayMicroseconds(10);",
                "  digitalWrite(trig, LOW);", "  unsigned long duration = pulseIn(echo, HIGH, 25000UL);",
                "  if (duration == 0) {", "    return 400;", "  }", "  return min(duration / 58.0, 400.0);", "}"});
        helper(out, "motor",
               {"void s2a_motor(int en, int in1, int in2, int direction, double speed) {",
                "  digitalWrite(in1, direction > 0 ? HIGH : LOW);", "  digitalWrite(in2, direction < 0 ? HIGH : LOW);",
                "  analogWrite(en, direction == 0 ? 0 : constrain((int)(speed * 2.55), 0, 255));", "}"});
        helper(out, "random",
               {"long s2a_random(double from, double to) {", "  long low = (long)min(from, to);",
                "  long high = (long)max(from, to);", "  return random(low, high + 1);", "}"});
        helper(out, "map",
               {"double s2a_map(double value, double fromLow, double fromHigh, double toLow, "
                "double toHigh) {",
                "  if (fromHigh == fromLow) {", "    return toLow;", "  }",
                "  return toLow + (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow);", "}"});

        Sketch sketch;
        if (!functionBodies_.empty()) {
            out.emplace_back("// Functions (\"define\" blocks), run by the \"call\" blocks.");
            for (const auto& [name, body] : functionBodies_) {
                out.push_back("void " + name + "();");
            }
            out.emplace_back("");
            for (const auto& [name, body] : functionBodies_) {
                out.push_back("void " + name + "() {");
                append(out, sketch, body);
                out.emplace_back("}");
                out.emplace_back("");
            }
        }
        if (tasks_) {
            out.insert(out.end(),
                       {"// Every script runs a little, then lets the others run (as in Scratch):",
                        "// a wait gives way to the other scripts instead of stopping the board.",
                        "struct S2ATask {", "  unsigned int line;", "  unsigned long wake;", "};",
                        "#define S2A_START(t) if ((t)->line == 0xFFFF) return; switch ((t)->line) { case 0:",
                        "#define S2A_END(t) } (t)->line = 0xFFFF",
                        "#define S2A_YIELD(t) do { (t)->line = __LINE__; return; case __LINE__:; } while (0)",
                        "#define S2A_WAIT(t, seconds) do { (t)->wake = millis() + s2a_ms(seconds); "
                        "(t)->line = __LINE__; case __LINE__: if ((long)(millis() - (t)->wake) < 0) return; } while (0)",
                        "#define S2A_WAIT_UNTIL(t, condition) do { (t)->line = __LINE__; case __LINE__: "
                        "if (!(condition)) return; } while (0)",
                        "", "unsigned long s2a_ms(double seconds) {",
                        "  return seconds > 0 ? (unsigned long)(seconds * 1000.0) : 0;", "}", ""});
            if (counters_ > 0) {
                std::string declaration = "long";
                for (int i = 1; i <= counters_; ++i) {
                    declaration += (i == 1 ? " s2a_i" : ", s2a_i") + std::to_string(i);
                }
                out.push_back(declaration + ";");
                out.emplace_back("");
            }
            for (std::size_t i = 0; i < tasks_bodies_.size(); ++i) {
                const std::string number = std::to_string(i + 1);
                out.push_back("S2ATask s2a_task" + number + ";");
                out.push_back("void s2a_script" + number + "() {");
                out.push_back(std::string(kIndent) + "S2ATask* const task = &s2a_task" + number + ";");
                out.push_back(std::string(kIndent) + "S2A_START(task);");
                append(out, sketch, tasks_bodies_[i]);
                out.push_back(std::string(kIndent) + "S2A_END(task);");
                out.emplace_back("}");
                out.emplace_back("");
            }
        }
        out.emplace_back("void setup() {");
        if (helpers_.count("serial") != 0) {
            out.push_back(std::string(kIndent) + "Serial.begin(" + std::to_string(kSerialBaud) + ");");
        }
        for (const auto& [label, mode] : pinModes_) {
            if (servos_.count(label) == 0) { // attach() sets the pin of a servo
                out.push_back(std::string(kIndent) + "pinMode(" + label + ", " + modeName(mode) + ");");
            }
        }
        for (const std::string& label : servos_) {
            out.push_back(std::string(kIndent) + "s2a_servo_" + label + ".attach(" + label + ");");
        }
        if (helpers_.count("random") != 0) {
            out.push_back(std::string(kIndent) + "randomSeed(micros());");
        }
        append(out, sketch, setup_);
        out.emplace_back("}");
        out.emplace_back("");
        out.emplace_back("void loop() {");
        append(out, sketch, loop_);
        for (std::size_t i = 0; i < tasks_bodies_.size(); ++i) {
            out.push_back(std::string(kIndent) + "s2a_script" + std::to_string(i + 1) + "();");
        }
        out.emplace_back("}");

        for (const std::string& line : out) {
            sketch.source += line;
            sketch.source += '\n';
        }
        return sketch;
    }

    static void append(std::vector<std::string>& out, Sketch& sketch, const Body& body) {
        const int firstLine = static_cast<int>(out.size()) + 1;
        for (const auto& [id, index] : body.blockLines) {
            sketch.blockLines.emplace(id, firstLine + index);
        }
        out.insert(out.end(), body.lines.begin(), body.lines.end());
    }

    const Project& project_;
    const BlockCatalog& catalog_;
    const Board* board_;
    Body setup_;
    Body loop_;
    bool tasks_ = false;
    std::vector<Body> tasks_bodies_; ///< One per script when several scripts run side by side.
    std::map<std::string, PinMode> pinModes_;
    std::map<std::string, std::string> variables_; ///< Variable id -> C++ name.
    std::map<std::string, std::string> functions_; ///< Function id -> C++ name.
    std::map<std::string, const project::Script*> definitions_; ///< Function id -> its "define" script.
    std::vector<std::pair<std::string, Body>> functionBodies_;  ///< Single-script programs.
    std::set<std::string> generatedFunctions_;
    std::set<std::string> inlining_; ///< Functions being written inside a script (no endless recursion).
    std::set<std::string> helpers_;
    std::set<std::string> servos_; ///< Pins driving a servo.
    int counters_ = 0;
};

} // namespace

std::array<int, 3> colourLevels(std::string_view name) {
    static const std::map<std::string_view, std::array<int, 3>> kColours = {
        {"RED", {255, 0, 0}},     {"GREEN", {0, 255, 0}},     {"BLUE", {0, 0, 255}},
        {"YELLOW", {255, 160, 0}}, {"CYAN", {0, 255, 255}},  {"MAGENTA", {255, 0, 255}},
        {"WHITE", {255, 255, 255}}, {"OFF", {0, 0, 0}},
    };
    const auto found = kColours.find(name);
    return found != kColours.end() ? found->second : std::array<int, 3>{0, 0, 0};
}

std::string stringLiteral(std::string_view text) {
    std::string result = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) {
                result += c; // UTF-8 bytes are kept: the serial monitor shows them
            }
        }
    }
    return result + "\"";
}

std::string numberLiteral(const std::string& text) {
    std::string trimmed;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            trimmed += c == ',' ? '.' : c;
        }
    }
    if (trimmed.empty()) {
        return "0";
    }
    std::istringstream stream(trimmed);
    stream.imbue(std::locale::classic());
    double value = 0;
    stream >> value;
    if (stream.fail() || !stream.eof() || !std::isfinite(value)) {
        return "0";
    }
    return formatNumber(value);
}

std::string identifier(const std::string& name) {
    // Accented Latin letters (UTF-8 C3 80..C3 BF) lose their accent: "été" -> "ete".
    static constexpr std::string_view kLatin1 = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
                                                "aaaaaaaceeeeiiiidnooooo_ouuuuyty";
    std::string result;
    const auto append = [&result](char c) {
        if (c == '_') {
            if (!result.empty() && result.back() != '_') {
                result += '_';
            }
        } else {
            result += c;
        }
    };
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (c < 0x80) {
            append(std::isalnum(c) != 0 ? static_cast<char>(c) : '_');
        } else if (c == 0xC3 && i + 1 < name.size()) {
            const auto next = static_cast<unsigned char>(name[++i]);
            const bool letter = next >= 0x80 && next <= 0xBF && next != 0x97 && next != 0xB7;
            append(letter ? kLatin1[next - 0x80] : '_');
        } else if (c >= 0xC0) {
            append('_'); // other characters: one separator per UTF-8 sequence
            while (i + 1 < name.size() && (static_cast<unsigned char>(name[i + 1]) & 0xC0U) == 0x80U) {
                ++i;
            }
        }
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    if (result.empty()) {
        return "variable";
    }
    if (std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(0, "_");
    }
    return result;
}

Sketch generate(const project::Project& project, const BlockCatalog& catalog, const Board* board) {
    return Generator(project, catalog, board).run();
}

Sketch generate(const project::Project& project) {
    return generate(project, BlockCatalog::builtin(), BoardDatabase::builtin().find(project.boardId));
}

} // namespace s2a::codegen
