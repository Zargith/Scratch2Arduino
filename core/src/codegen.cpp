#include "s2a/codegen.hpp"

#include <algorithm>
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

enum class PinMode { Input, Output };

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
    }

    Sketch run() {
        for (const project::Script& script : project_.scripts) {
            if (script.blocks.empty() || !isHat(script.blocks.front())) {
                continue; // Scratch rule: loose blocks do not run.
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
    bool isHat(const Block& block) const {
        const BlockDefinition* definition = catalog_.find(block.type);
        return definition != nullptr && definition->shape == BlockShape::Hat;
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

    std::string pin(const Block& block, const std::string& name, PinMode mode) {
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
        // A pin used as an output anywhere stays an output.
        auto [it, inserted] = pinModes_.emplace(label, mode);
        if (!inserted && mode == PinMode::Output) {
            it->second = PinMode::Output;
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
            return "(digitalRead(" + pin(block, "PIN", PinMode::Input) + ") == HIGH)";
        }
        if (type == "pins_analog_read") {
            return "analogRead(" + pin(block, "PIN", PinMode::Input) + ")";
        }
        if (type == "variables_get") {
            const std::string name = variable(block);
            return name.empty() ? "0" : name;
        }
        const BlockDefinition* definition = catalog_.find(type);
        return definition != nullptr && definition->shape == BlockShape::Boolean ? "false" : "0";
    }

    // --- statements ------------------------------------------------------------------

    void statements(Body& body, const Block& parent, const std::string& name, int depth) {
        auto found = parent.statements.find(name);
        if (found == parent.statements.end()) {
            return;
        }
        for (const Block& block : found->second) {
            statement(body, block, depth);
        }
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
        } else if (type == "pins_analog_write") {
            const std::string target = pin(block, "PIN", PinMode::Output);
            body.add(depth, "analogWrite(" + target + ", constrain((int)(" + number(block, "VALUE") +
                                "), 0, 255));");
        } else if (type == "control_wait") {
            helpers_.insert("wait");
            body.add(depth, "s2a_wait(" + number(block, "SECONDS") + ");");
        } else if (type == "control_wait_until") {
            body.add(depth, "while (!" + parenthesized(condition(block, "CONDITION")) + ") {");
            body.add(depth + 1, "delay(1);");
            body.add(depth, "}");
        } else if (type == "control_repeat") {
            const std::string counter = "i" + std::to_string(++counters_);
            body.add(depth, "for (long " + counter + " = 0; " + counter + " < (long)(" +
                                number(block, "TIMES") + "); ++" + counter + ") {");
            statements(body, block, "SUBSTACK", depth + 1);
            body.add(depth, "}");
        } else if (type == "control_forever") {
            body.add(depth, "for (;;) {");
            statements(body, block, "SUBSTACK", depth + 1);
            body.add(depth + 1, "yield();");
            body.add(depth, "}");
        } else if (type == "control_if" || type == "control_if_else") {
            body.add(depth, "if " + parenthesized(condition(block, "CONDITION")) + " {");
            statements(body, block, "SUBSTACK", depth + 1);
            if (type == "control_if_else") {
                body.add(depth, "} else {");
                statements(body, block, "SUBSTACK2", depth + 1);
            }
            body.add(depth, "}");
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

        for (const project::Variable& variable : project_.variables) {
            out.push_back("double " + variables_.at(variable.id) + " = " +
                          numberLiteral(formatNumber(variable.initialValue)) + "; // " +
                          oneLine(variable.name));
        }
        if (!project_.variables.empty()) {
            out.emplace_back("");
        }

        helper(out, "wait",
               {"void s2a_wait(double seconds) {", "  if (seconds > 0) {",
                "    delay((unsigned long)(seconds * 1000.0));", "  }", "}"});
        helper(out, "random",
               {"long s2a_random(double from, double to) {", "  long low = (long)min(from, to);",
                "  long high = (long)max(from, to);", "  return random(low, high + 1);", "}"});
        helper(out, "map",
               {"double s2a_map(double value, double fromLow, double fromHigh, double toLow, "
                "double toHigh) {",
                "  if (fromHigh == fromLow) {", "    return toLow;", "  }",
                "  return toLow + (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow);", "}"});

        Sketch sketch;
        out.emplace_back("void setup() {");
        for (const auto& [label, mode] : pinModes_) {
            out.push_back(std::string(kIndent) + "pinMode(" + label + ", " +
                          (mode == PinMode::Output ? "OUTPUT" : "INPUT") + ");");
        }
        if (helpers_.count("random") != 0) {
            out.push_back(std::string(kIndent) + "randomSeed(micros());");
        }
        append(out, sketch, setup_);
        out.emplace_back("}");
        out.emplace_back("");
        out.emplace_back("void loop() {");
        append(out, sketch, loop_);
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
    std::map<std::string, PinMode> pinModes_;
    std::map<std::string, std::string> variables_; ///< Variable id -> C++ name.
    std::set<std::string> helpers_;
    int counters_ = 0;
};

} // namespace

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
