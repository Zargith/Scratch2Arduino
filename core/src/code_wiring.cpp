// Components used by code typed as text (expert mode, "My code"), found from the calls it makes,
// so that the wiring diagram also works for programs written in C++, Python or JavaScript.
#include "s2a/convert.hpp"
#include "s2a/wiring.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace s2a::wiring {

namespace {

using project::Block;

/// The code with comments and the inside of strings blanked out (line breaks kept).
std::string blankCommentsAndStrings(const std::string& code) {
    std::string out = code;
    enum class State { Code, Line, Block, String, Char } state = State::Code;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const char c = out[i];
        const char next = i + 1 < out.size() ? out[i + 1] : '\0';
        switch (state) {
        case State::Code:
            if (c == '/' && next == '/') {
                state = State::Line;
                out[i] = ' ';
            } else if (c == '/' && next == '*') {
                state = State::Block;
                out[i] = ' ';
            } else if (c == '"') {
                state = State::String;
            } else if (c == '\'') {
                state = State::Char;
            }
            break;
        case State::Line:
            if (c == '\n') {
                state = State::Code;
            } else {
                out[i] = ' ';
            }
            break;
        case State::Block:
            if (c == '*' && next == '/') {
                out[i] = ' ';
                out[i + 1] = ' ';
                ++i;
                state = State::Code;
            } else if (c != '\n') {
                out[i] = ' ';
            }
            break;
        case State::String:
        case State::Char:
            if (c == '\\' && next != '\0' && next != '\n') {
                out[i] = ' ';
                out[i + 1] = ' ';
                ++i;
            } else if (c == (state == State::String ? '"' : '\'') || c == '\n') {
                state = State::Code;
            } else {
                out[i] = ' ';
            }
            break;
        }
    }
    return out;
}

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// A call (or a constructor) found in the code: "obj.attach(9)" -> object "obj", name "attach".
struct Call {
    std::string type;   ///< Word just before the name ("Servo" in "Servo s(9)"), "" when none.
    std::string object; ///< "obj" of "obj.name(...)".
    std::string name;
    std::vector<std::string> args;
    int line = 1;
};

std::vector<Call> callsIn(const std::string& code) {
    std::vector<Call> calls;
    int line = 1;
    std::size_t lineCounted = 0;
    const auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (code[i] != '(' || i == 0) {
            continue;
        }
        std::size_t end = i;
        while (end > 0 && (code[end - 1] == ' ' || code[end - 1] == '\t')) {
            --end;
        }
        std::size_t start = end;
        while (start > 0 && isWord(code[start - 1])) {
            --start;
        }
        if (start == end || std::isdigit(static_cast<unsigned char>(code[start])) != 0) {
            continue;
        }
        Call call;
        call.name = code.substr(start, end - start);
        std::size_t before = start;
        if (before > 0 && (code[before - 1] == '.' || (before > 1 && code[before - 1] == '>' && code[before - 2] == '-'))) {
            std::size_t objectEnd = code[before - 1] == '.' ? before - 1 : before - 2;
            std::size_t objectStart = objectEnd;
            while (objectStart > 0 && isWord(code[objectStart - 1])) {
                --objectStart;
            }
            call.object = code.substr(objectStart, objectEnd - objectStart);
            before = objectStart;
        } else {
            std::size_t typeEnd = before;
            while (typeEnd > 0 && (code[typeEnd - 1] == ' ' || code[typeEnd - 1] == '\t' || code[typeEnd - 1] == '*')) {
                --typeEnd;
            }
            std::size_t typeStart = typeEnd;
            while (typeStart > 0 && isWord(code[typeStart - 1])) {
                --typeStart;
            }
            if (typeEnd > typeStart && typeEnd < before) {
                call.type = code.substr(typeStart, typeEnd - typeStart);
            }
        }
        // Arguments, split at the commas outside brackets.
        int depth = 0;
        std::string arg;
        std::size_t j = i + 1;
        for (; j < code.size(); ++j) {
            const char c = code[j];
            if (c == '(' || c == '[' || c == '{') {
                ++depth;
            } else if (c == ')' || c == ']' || c == '}') {
                if (depth == 0) {
                    break;
                }
                --depth;
            } else if (c == ',' && depth == 0) {
                call.args.push_back(trim(arg));
                arg.clear();
                continue;
            }
            arg.push_back(c);
        }
        if (j >= code.size()) {
            continue;
        }
        if (!trim(arg).empty() || !call.args.empty()) {
            call.args.push_back(trim(arg));
        }
        line += static_cast<int>(std::count(code.begin() + static_cast<std::ptrdiff_t>(lineCounted),
                                            code.begin() + static_cast<std::ptrdiff_t>(i), '\n'));
        lineCounted = i;
        call.line = line;
        calls.push_back(std::move(call));
    }
    return calls;
}

/// Simple named pins: "const int led = 13;", "#define BUTTON 2", "int pot = A0;". A name given
/// several different values is left out (it is not a pin number then).
std::map<std::string, std::string> namedPins(const std::string& code) {
    static const std::regex define(R"(#define\s+(\w+)\s+((?:A|D|GPIO)?\d+)\b)");
    static const std::regex declaration(
        R"(\b(?:const|constexpr|static|unsigned|signed|int|byte|uint8_t|uint16_t|int8_t|long|short|char|auto|let|var|float|double)\s+(\w+)\s*=\s*((?:A|D)?\d+)\s*[;,)])");
    std::map<std::string, std::string> names;
    std::set<std::string> ambiguous;
    const auto add = [&](const std::string& name, const std::string& value) {
        if (const auto found = names.find(name); found != names.end() && found->second != value) {
            ambiguous.insert(name);
        }
        names[name] = value;
    };
    for (const std::regex* pattern : {&define, &declaration}) {
        for (auto it = std::sregex_iterator(code.begin(), code.end(), *pattern); it != std::sregex_iterator(); ++it) {
            add((*it)[1].str(), (*it)[2].str());
        }
    }
    // Assigned again later ("led = 5;"): not a fixed pin.
    static const std::regex assignment(R"((?:^|[^\w.=!<>+\-*/])(\w+)\s*=\s*([^=;]+);)");
    for (auto it = std::sregex_iterator(code.begin(), code.end(), assignment); it != std::sregex_iterator(); ++it) {
        const std::string name = (*it)[1].str();
        if (const auto found = names.find(name); found != names.end() && trim((*it)[2].str()) != found->second) {
            ambiguous.insert(name);
        }
    }
    // Counted or changed in place ("pin++", "--pin", "pin += 2"): a loop variable, not a pin.
    static const std::regex changed(R"((\w+)\s*(?:\+\+|--|[-+*/%]=)|(?:\+\+|--)\s*(\w+))");
    for (auto it = std::sregex_iterator(code.begin(), code.end(), changed); it != std::sregex_iterator(); ++it) {
        ambiguous.insert((*it)[1].matched ? (*it)[1].str() : (*it)[2].str());
    }
    for (const std::string& name : ambiguous) {
        names.erase(name);
    }
    return names;
}

/// Collects the components, one block each (fields = pin arguments of the catalog).
class Finder {
public:
    explicit Finder(const std::string& code) : names_(namedPins(code)) {}

    /// "13", "A0", "D5", or a named pin; nullopt for anything else (a variable, LED_BUILTIN).
    [[nodiscard]] std::optional<std::string> pin(const std::string& text) const {
        static const std::regex literal(R"(^(?:A|D)?\d+$)");
        const std::string token = trim(text);
        if (std::regex_match(token, literal)) {
            return token;
        }
        if (const auto found = names_.find(token); found != names_.end()) {
            return found->second;
        }
        return std::nullopt;
    }

    void add(int line, const std::string& type, std::map<std::string, std::string> fields) {
        for (const auto& [name, value] : fields) {
            if ((name.size() > 4 && name.compare(name.size() - 4, 4, "_PIN") == 0) || name == "PIN") {
                claimed_.insert(value);
            }
        }
        Block block;
        block.id = "code-" + std::to_string(line);
        block.type = type;
        block.fields = std::move(fields);
        blocks_.emplace_back(line, std::move(block));
    }

    [[nodiscard]] bool claimed(const std::string& pin) const { return claimed_.count(pin) != 0; }

    [[nodiscard]] project::Stack stack() {
        std::stable_sort(blocks_.begin(), blocks_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        project::Stack stack;
        for (auto& [line, block] : blocks_) {
            stack.push_back(std::move(block));
        }
        return stack;
    }

private:
    std::map<std::string, std::string> names_;
    std::set<std::string> claimed_;
    std::vector<std::pair<int, Block>> blocks_;
};

} // namespace

project::Script componentsInCode(std::string_view source, project::ProgramLanguage language) {
    std::string code(source);
    if (language == project::ProgramLanguage::Python || language == project::ProgramLanguage::JavaScript) {
        code = convert::convert(source, language, project::ProgramLanguage::Cpp).code;
    }
    code = blankCommentsAndStrings(code);
    Finder finder(code);
    const std::vector<Call> calls = callsIn(code);

    // Objects of the component libraries, and the screens without pins (I2C).
    std::set<std::string> servos;
    static const std::regex servo(R"(\bServo\s+(\w+))");
    for (auto it = std::sregex_iterator(code.begin(), code.end(), servo); it != std::sregex_iterator(); ++it) {
        servos.insert((*it)[1].str());
    }
    const auto lineOf = [&](std::size_t position) {
        return 1 + static_cast<int>(std::count(code.begin(), code.begin() + static_cast<std::ptrdiff_t>(position), '\n'));
    };
    if (const auto at = code.find("LiquidCrystal_I2C"); at != std::string::npos) {
        finder.add(lineOf(at), "display_lcd_print", {});
    }
    for (const char* oled : {"U8X8_SSD1306", "U8G2_SSD1306", "Adafruit_SSD1306"}) {
        if (const auto at = code.find(oled); at != std::string::npos) {
            finder.add(lineOf(at), "display_oled_print", {});
            break;
        }
    }

    // Components found from their calls.
    std::map<std::string, std::string> modes; ///< Pin -> "OUTPUT", "INPUT", "INPUT_PULLUP".
    std::vector<std::pair<int, std::string>> reads, analogReads, writes;
    std::optional<std::string> lastWritten; ///< For pulseIn(): the trigger pin written just before.
    for (const Call& call : calls) {
        const auto& a = call.args;
        const auto pinAt = [&](std::size_t index) { return index < a.size() ? finder.pin(a[index]) : std::nullopt; };
        const std::string& type = call.type.empty() ? call.name : call.type;
        if (type == "DHT" && a.size() >= 2) {
            if (const auto p = pinAt(0)) {
                finder.add(call.line, "sensors_temperature", {{"PIN", *p}, {"MODEL", trim(a[1]) == "DHT22" ? "DHT22" : "DHT11"}});
            }
        } else if (type == "Adafruit_NeoPixel" && a.size() >= 2) {
            if (const auto p = pinAt(1)) {
                finder.add(call.line, "lights_neopixel_fill", {{"PIN", *p}});
            }
        } else if (type == "LiquidCrystal" && a.size() == 6) {
            const auto rs = pinAt(0), e = pinAt(1), d4 = pinAt(2), d5 = pinAt(3), d6 = pinAt(4), d7 = pinAt(5);
            if (rs && e && d4 && d5 && d6 && d7) {
                finder.add(call.line, "display_lcd16_print",
                           {{"RS_PIN", *rs}, {"E_PIN", *e}, {"D4_PIN", *d4}, {"D5_PIN", *d5}, {"D6_PIN", *d6}, {"D7_PIN", *d7}});
            }
        } else if (type == "SoftwareSerial" && a.size() >= 2) {
            const auto rx = pinAt(0), tx = pinAt(1);
            if (rx && tx) {
                finder.add(call.line, "phone_module", {{"RX_PIN", *rx}, {"TX_PIN", *tx}});
            }
        } else if ((type == "LedControl" || call.name == "s2a_matrix_init") && a.size() >= 3) {
            const auto din = pinAt(0), clk = pinAt(1), cs = pinAt(2);
            if (din && clk && cs) {
                finder.add(call.line, "lights_matrix_show", {{"DIN_PIN", *din}, {"CLK_PIN", *clk}, {"CS_PIN", *cs}});
            }
        } else if (call.name == "attach" && servos.count(call.object) != 0) {
            if (const auto p = pinAt(0)) {
                finder.add(call.line, "motors_servo_write", {{"PIN", *p}});
            }
        } else if (call.name == "servoWrite") {
            if (const auto p = pinAt(0)) {
                finder.add(call.line, "motors_servo_write", {{"PIN", *p}});
            }
        } else if (call.name == "tone" || call.name == "noTone" || call.name == "s2a_tone" || call.name == "s2a_quiet") {
            if (const auto p = pinAt(0)) {
                finder.add(call.line, "sound_play_tone", {{"PIN", *p}});
            }
        } else if (call.name == "s2a_distance") {
            const auto trig = pinAt(0), echo = pinAt(1);
            if (trig && echo) {
                finder.add(call.line, "sensors_distance", {{"TRIG_PIN", *trig}, {"ECHO_PIN", *echo}});
            }
        } else if (call.name == "pulseIn") {
            if (const auto echo = pinAt(0); echo && lastWritten && *lastWritten != *echo) {
                finder.add(call.line, "sensors_distance", {{"TRIG_PIN", *lastWritten}, {"ECHO_PIN", *echo}});
            }
        } else if (call.name == "s2a_motor") {
            const auto en = pinAt(0), in1 = pinAt(1), in2 = pinAt(2);
            if (en && in1 && in2) {
                finder.add(call.line, "motors_dc_run", {{"EN_PIN", *en}, {"IN1_PIN", *in1}, {"IN2_PIN", *in2}});
            }
        } else if (call.name == "pinMode" && a.size() >= 2) {
            if (const auto p = pinAt(0)) {
                modes[*p] = trim(a[1]);
                if (modes[*p] == "OUTPUT") {
                    writes.emplace_back(call.line, *p);
                } else {
                    reads.emplace_back(call.line, *p);
                }
            }
        } else if (call.name == "digitalRead") {
            if (const auto p = pinAt(0)) {
                reads.emplace_back(call.line, *p);
            }
        } else if (call.name == "analogRead") {
            if (const auto p = pinAt(0)) {
                analogReads.emplace_back(call.line, *p);
            }
        } else if (call.name == "digitalWrite" || call.name == "analogWrite") {
            if (const auto p = pinAt(0)) {
                writes.emplace_back(call.line, *p);
                lastWritten = p;
            }
        }
    }

    // Plain pins, unless a component above already uses them.
    std::set<std::string> done;
    for (const auto& [line, pin] : analogReads) {
        if (!finder.claimed(pin) && done.insert(pin).second) {
            finder.add(line, "pins_analog_read", {{"PIN", pin}});
        }
    }
    for (const auto& [line, pin] : reads) {
        if (!finder.claimed(pin) && done.insert(pin).second) {
            const auto mode = modes.find(pin);
            const bool pullUp = mode != modes.end() && mode->second == "INPUT_PULLUP";
            finder.add(line, pullUp ? "sensors_button_pressed" : "pins_digital_read", {{"PIN", pin}});
        }
    }
    for (const auto& [line, pin] : writes) {
        if (!finder.claimed(pin) && done.insert(pin).second) {
            finder.add(line, "pins_digital_write", {{"PIN", pin}});
        }
    }

    project::Script script;
    Block start;
    start.id = "code-start";
    start.type = "events_when_program_starts";
    script.blocks.push_back(std::move(start));
    for (Block& block : finder.stack()) {
        script.blocks.push_back(std::move(block));
    }
    return script;
}

} // namespace s2a::wiring
