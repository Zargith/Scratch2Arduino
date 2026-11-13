#include "s2a/codegen.hpp"
#include "s2a/convert.hpp"

#include "s2a/usercode.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
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
            // Its inputs: C++ parameters "arg_<name>", unique within the function.
            std::set<std::string> taken;
            auto& parameters = parameters_[function.id];
            for (std::size_t i = 0; i < function.parameters.size() && i < project::kMaxFunctionParameters; ++i) {
                const std::string stem = "arg_" + identifier(function.parameters[i]);
                std::string parameter = stem;
                for (int suffix = 2; taken.count(parameter) != 0; ++suffix) {
                    parameter = stem + "_" + std::to_string(suffix);
                }
                taken.insert(parameter);
                parameters.emplace_back(function.parameters[i], parameter);
            }
        }
        for (const project::Script& script : project.scripts) {
            if (!script.blocks.empty() && script.blocks.front().type == kDefine) {
                definitions_.emplace(field(script.blocks.front(), "FUNCTION"), &script);
            }
            for (const Block& block : script.blocks) {
                findSharedPins(block);
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
        const std::string id = field(script.blocks.front(), "FUNCTION");
        std::string signature = "void " + name->second + "(";
        Scope scope;
        for (const auto& [input, parameter] : parameters_[id]) {
            signature += (scope.empty() ? "double " : ", double ") + parameter;
            scope.emplace(input, parameter);
        }
        signature += ")";
        scopes_.push_back(std::move(scope));
        Body body;
        body.mark(script.blocks.front().id);
        for (std::size_t i = 1; i < script.blocks.size(); ++i) {
            statement(body, script.blocks[i], 1);
        }
        scopes_.pop_back();
        functionBodies_.emplace_back(signature, std::move(body));
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
            // A String of "My code" in a number slot: its number ("12.5" -> 12.5).
            return expression(*found->second.block) + (isUserText(*found->second.block) ? ".toFloat()" : "");
        }
        return numberLiteral(found->second.value);
    }

    /// A "My code" block giving an Arduino String (a String variable, or a function returning one).
    bool isUserText(const Block& block) const {
        const bool get = block.type == usercode::kGetBlock;
        if (!get && block.type != usercode::kValueBlock) {
            return false;
        }
        const auto* found =
            userDeclaration(block, get ? usercode::Declaration::Kind::Variable : usercode::Declaration::Kind::Function);
        return found != nullptr && found->text;
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
        if (type == "sensors_joystick") {
            const std::string axis = field(block, "AXIS") == "Y" ? "Y_PIN" : "X_PIN";
            return "(analogRead(" + pin(block, axis, PinMode::Input) + ") * 100.0 / 1023.0)";
        }
        if (type == "sensors_joystick_direction") {
            // Pushed far: below a quarter or above three quarters of the way.
            const std::string direction = field(block, "DIRECTION");
            const bool horizontal = direction == "LEFT" || direction == "RIGHT";
            const std::string read = "analogRead(" + pin(block, horizontal ? "X_PIN" : "Y_PIN", PinMode::Input) + ")";
            return "(" + read + (direction == "LEFT" || direction == "UP" ? " < 256)" : " > 767)");
        }
        if (type == "sensors_joystick_pressed") {
            return "(digitalRead(" + pin(block, "SW_PIN", PinMode::InputPullup) + ") == LOW)";
        }
        if (type == "sensors_motion") {
            return "(digitalRead(" + pin(block, "PIN", PinMode::Input) + ") == HIGH)";
        }
        if (type == "sensors_obstacle") {
            // The sensor pulls its output LOW when something is in front of it.
            return "(digitalRead(" + pin(block, "PIN", PinMode::InputPullup) + ") == LOW)";
        }
        if (type == "sensors_distance") {
            helpers_.insert("distance");
            const std::string trig = pin(block, "TRIG_PIN", PinMode::Output);
            return "s2a_distance(" + trig + ", " + pin(block, "ECHO_PIN", PinMode::Input) + ")";
        }
        if (type == "sound_note") {
            return numberLiteral(field(block, "NOTE"));
        }
        if (type == "phone_received") {
            phone();
            return "s2a_phone_received(" + phoneText(block, "MESSAGE") + ")";
        }
        if (type == "phone_number") {
            phone();
            return "s2a_phone_number()";
        }
        if (type == "sensors_temperature" || type == "sensors_humidity") {
            const std::string label = pinLabel(block, "PIN");
            const std::string sensor = "s2a_dht_" + label;
            const std::string model = field(block, "MODEL") == "DHT22" ? "DHT22" : "DHT11";
            if (dhtSensors_.insert(label).second) { // the first block gives the model of a pin
                use("DHT.h", "DHT " + sensor + "(" + label + ", " + model + ");", sensor + ".begin();", label);
                globals_.push_back("double " + sensor + "_last[2]; // last good temperature and humidity");
            }
            helpers_.insert("dht");
            return "s2a_dht_read(" + sensor + ", " + sensor + "_last, " +
                   (type == "sensors_humidity" ? "true" : "false") + ")";
        }
        if (type == "variables_get") {
            const std::string name = variable(block);
            return name.empty() ? "0" : name;
        }
        if (type == "functions_parameter") {
            // The number given by "call"; outside its function (reported by the validator): 0.
            if (!scopes_.empty()) {
                if (const auto found = scopes_.back().find(field(block, "NAME")); found != scopes_.back().end()) {
                    return found->second;
                }
            }
            return "0";
        }
        if (type == usercode::kValueBlock) {
            const std::string call = userCall(block, true);
            return call.empty() ? "0" : call;
        }
        if (type == usercode::kGetBlock) {
            const auto* found = userDeclaration(block, usercode::Declaration::Kind::Variable);
            return found != nullptr ? found->name : "0";
        }
        const BlockDefinition* definition = catalog_.find(type);
        return definition != nullptr && definition->shape == BlockShape::Boolean ? "false" : "0";
    }

    // --- "My code" (C++ only: Python and JavaScript run in Live mode) ---------------

    const usercode::Declaration* userDeclaration(const Block& block, usercode::Declaration::Kind kind) const {
        if (!project_.userCode || project_.userCode->language != project::ProgramLanguage::Cpp) {
            return nullptr;
        }
        return usercode::find(userDeclarations_, field(block, std::string(usercode::kNameField)), kind);
    }

    /// "blink(3, 200)", or "" when the function is not in the code (reported by the validator).
    std::string userCall(const Block& block, bool value) {
        const auto* found = userDeclaration(block, usercode::Declaration::Kind::Function);
        if (found == nullptr || (value && !found->returnsValue)) {
            return {};
        }
        std::string call = found->name + "(";
        for (std::size_t i = 0; i < found->parameters.size(); ++i) {
            call += (i > 0 ? ", " : "") + number(block, "ARG" + std::to_string(i + 1));
        }
        return call + ")";
    }

    std::string functionName(const std::string& id) const {
        for (const project::Function& function : project_.functions) {
            if (function.id == id) {
                return function.name;
            }
        }
        return id;
    }

    // --- libraries -------------------------------------------------------------------

    /// Declares a library object once: its header, global declaration, set-up line and pin.
    void use(const std::string& header, const std::string& global, const std::string& init,
             const std::string& pin = {}) {
        includes_.insert(header);
        if (std::find(globals_.begin(), globals_.end(), global) == globals_.end()) {
            globals_.push_back(global);
            if (!init.empty()) {
                inits_.push_back(init);
            }
        }
        if (!pin.empty()) {
            ownedPins_.insert(pin);
        }
    }

    /// The Bluetooth serial link to the phone, set up in setup(): the ESP32's own Bluetooth, or an
    /// HC-05 / HC-06 module on an Arduino board (the pins of its "Bluetooth module" block).
    void phone() {
        helpers_.insert("phone");
        helpers_.insert("text");
        if (board_ != nullptr && board_->bluetoothModule) {
            std::string rx = "10";
            std::string tx = "11";
            if (const Block* module = moduleBlock()) {
                rx = pinLabel(*module, "RX_PIN");
                tx = pinLabel(*module, "TX_PIN");
            }
            const Pin* rxPin = board_->findPin(rx);
            const Pin* txPin = board_->findPin(tx);
            const int port = rxPin != nullptr && txPin != nullptr ? moduleSerialPort(*board_, rxPin->number, txPin->number) : 0;
            if (port > 0) { // a serial port of the Mega
                use("Arduino.h", "HardwareSerial& s2a_phone = Serial" + std::to_string(port) + ";", "s2a_phone.begin(9600);");
            } else {
                use("SoftwareSerial.h", "SoftwareSerial s2a_phone(" + rx + ", " + tx + ");", "s2a_phone.begin(9600);");
            }
            ownedPins_.insert(rx);
            ownedPins_.insert(tx);
            return;
        }
        use("BluetoothSerial.h", "BluetoothSerial s2a_phone;", "s2a_phone_begin();");
        includes_.insert("esp_mac.h"); // esp_read_mac, for the name of the board
        helpers_.insert("phone_esp32");
    }

    /// The first "Bluetooth module" block of the scripts that run (the validator checks the others agree).
    const Block* moduleBlock() const {
        const Block* found = nullptr;
        for (const project::Script& script : project_.scripts) {
            const BlockDefinition* first = script.blocks.empty() ? nullptr : catalog_.find(script.blocks.front().type);
            if (first == nullptr || first->shape != BlockShape::Hat) {
                continue;
            }
            project::forEachBlock(script.blocks, [&found](const Block& block) {
                if (found == nullptr && block.type == "phone_module") {
                    found = &block;
                }
            });
        }
        return found;
    }

    /// A text slot as a String expression, accents kept (the phone shows them).
    std::string phoneText(const Block& block, const std::string& name) {
        const auto input = block.inputs.find(name);
        if (input != block.inputs.end() && input->second.block) {
            return "s2a_text(" + expression(*input->second.block) + ")";
        }
        const std::string text = input != block.inputs.end() ? input->second.value : defaultValue(block, name);
        return "String(" + stringLiteral(text) + ")";
    }

    /// A text slot as a String expression: typed text (accents removed, for screens) or the
    /// value of a reporter written like the serial monitor does.
    std::string displayText(const Block& block, const std::string& name) {
        const auto input = block.inputs.find(name);
        if (input != block.inputs.end() && input->second.block) {
            helpers_.insert("text");
            return "s2a_text(" + expression(*input->second.block) + ")";
        }
        const std::string text = input != block.inputs.end() ? input->second.value : defaultValue(block, name);
        return "F(" + stringLiteral(asciiText(text)) + ")";
    }

    /// Value typed in a number slot (no reporter), for sizes fixed when the program starts.
    std::optional<double> literal(const Block& block, const std::string& name) const {
        const auto input = block.inputs.find(name);
        if (input != block.inputs.end() && input->second.block) {
            return std::nullopt;
        }
        std::istringstream stream(
            numberLiteral(input != block.inputs.end() ? input->second.value : defaultValue(block, name)));
        stream.imbue(std::locale::classic());
        double value = 0;
        stream >> value;
        return value;
    }

    std::string strip(const Block& block) {
        const std::string label = pinLabel(block, "PIN");
        const std::string name = "s2a_strip_" + label;
        const auto count = literal(block, "COUNT");
        const int pixels = count ? std::clamp(static_cast<int>(*count), 1, 100) : 8;
        auto& known = stripLengths_[label];
        known = std::max(known, pixels);
        includes_.insert("Adafruit_NeoPixel.h");
        ownedPins_.insert(label);
        return name;
    }

    std::string matrix(const Block& block) {
        const std::string din = pin(block, "DIN_PIN", PinMode::Output);
        const std::string clk = pin(block, "CLK_PIN", PinMode::Output);
        const std::string cs = pin(block, "CS_PIN", PinMode::Output);
        const std::string rows = "s2a_matrix_" + din + "_" + clk + "_" + cs;
        helpers_.insert("matrix");
        if (std::find(matrices_.begin(), matrices_.end(), rows) == matrices_.end()) {
            matrices_.push_back(rows);
            matrixInits_.push_back("s2a_matrix_init(" + din + ", " + clk + ", " + cs + ");");
        }
        return din + ", " + clk + ", " + cs + ", " + rows;
    }

    /// Pins used by other blocks than the buzzer ones: a buzzer pin is only released when
    /// nothing else drives it.
    void findSharedPins(const Block& block) {
        if (block.type.rfind("sound_", 0) != 0) {
            if (const BlockDefinition* definition = catalog_.find(block.type)) {
                for (const BlockArg& arg : definition->args) {
                    if (arg.kind == ArgKind::Pin) {
                        sharedPins_.insert(pinLabel(block, arg.name));
                    }
                }
            }
        }
        for (const auto& [name, input] : block.inputs) {
            if (input.block) {
                findSharedPins(*input.block);
            }
        }
        for (const auto& [name, stack] : block.statements) {
            for (const Block& inner : stack) {
                findSharedPins(inner);
            }
        }
    }

    /// Pin of a buzzer block. A pin only used by buzzers is left alone until a sound starts
    /// (tone() sets it) and released when it stops (see s2a_quiet).
    std::string buzzerPin(const Block& block) {
        const std::string label = pinLabel(block, "PIN");
        return sharedPins_.count(label) != 0 ? pin(block, "PIN", PinMode::Output) : label;
    }

    std::string silence(const std::string& target) {
        if (sharedPins_.count(target) != 0) {
            return "noTone(" + target + ");";
        }
        helpers_.insert("quiet");
        return "s2a_quiet(" + target + ");";
    }

    [[nodiscard]] bool esp32() const { return board_ != nullptr && board_->architecture == "esp32"; }

    /// The on-board LED: LED_BUILTIN on Arduino boards, its GPIO number on the others (not every
    /// ESP32 board defines LED_BUILTIN).
    [[nodiscard]] std::string builtinLed() const {
        return board_ != nullptr && board_->architecture != "avr" && board_->builtinLed >= 0
                   ? std::to_string(board_->builtinLed)
                   : std::string("LED_BUILTIN");
    }

    /// LCD screen with 16 pins (no I2C module), driven in 4-bit mode: one object per wiring.
    std::string parallelLcd(const Block& block) {
        std::string pins;
        std::string name = "s2a_lcd16";
        for (const char* arg : {"RS_PIN", "E_PIN", "D4_PIN", "D5_PIN", "D6_PIN", "D7_PIN"}) {
            const std::string label = pinLabel(block, arg);
            pins += (pins.empty() ? "" : ", ") + label;
            name += "_" + label;
            ownedPins_.insert(label); // the library sets the pins it drives
        }
        use("LiquidCrystal.h", "LiquidCrystal " + name + "(" + pins + ");", name + ".begin(16, 2);");
        return name;
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
            const std::string led = builtinLed();
            pinModes_[led] = PinMode::Output;
            // On some boards (ESP8266) the LED lights when its pin is LOW.
            const bool lit = field(block, "STATE") != "OFF";
            const bool high = board_ != nullptr && board_->builtinLedActiveLow ? !lit : lit;
            body.add(depth, "digitalWrite(" + led + ", " + (high ? "HIGH" : "LOW") + ");");
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
        } else if (type == "phone_send") {
            phone();
            body.add(depth, "s2a_phone.println(" + phoneText(block, "VALUE") + ");");
        } else if (type == "phone_module") {
            phone(); // the link starts in setup()
            body.add(depth, "// Bluetooth module: TXD to pin " + pinLabel(block, "RX_PIN") + ", RXD to pin " +
                                pinLabel(block, "TX_PIN"));
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
            const std::string target = buzzerPin(block);
            body.add(depth, "s2a_tone(" + target + ", " + number(block, "FREQUENCY") + ");");
            if (type == "sound_play_tone") {
                wait(body, number(block, "SECONDS"), depth);
                body.add(depth, silence(target));
            }
        } else if (type == "sound_stop") {
            body.add(depth, silence(buzzerPin(block)));
        } else if (type == "display_lcd_print" || type == "display_lcd_clear") {
            helpers_.insert("lcd");
            use("LiquidCrystal_I2C.h", "LiquidCrystal_I2C* s2a_lcd = nullptr;", "s2a_lcd_begin();");
            includes_.insert("Wire.h");
            if (type == "display_lcd_clear") {
                body.add(depth, "s2a_lcd->clear();");
            } else {
                body.add(depth, "s2a_lcd_line(" + std::string(field(block, "LINE") == "2" ? "2" : "1") + ", " +
                                    displayText(block, "TEXT") + ");");
            }
        } else if (type == "display_lcd16_print" || type == "display_lcd16_clear") {
            const std::string screen = parallelLcd(block);
            if (type == "display_lcd16_clear") {
                body.add(depth, screen + ".clear();");
            } else {
                helpers_.insert("lcd16");
                body.add(depth, "s2a_lcd16_line(" + screen + ", " + (field(block, "LINE") == "2" ? "2" : "1") + ", " +
                                    displayText(block, "TEXT") + ");");
            }
        } else if (type == "display_oled_print" || type == "display_oled_clear") {
            helpers_.insert("oled");
            use("U8x8lib.h", "U8X8_SSD1306_128X64_NONAME_HW_I2C s2a_oled(U8X8_PIN_NONE);",
                "s2a_oled.begin();\n  s2a_oled.setFont(u8x8_font_chroma48medium8_r);");
            if (type == "display_oled_clear") {
                body.add(depth, "s2a_oled.clear();");
            } else {
                const std::string line = field(block, "LINE");
                const bool valid = line.size() == 1 && line[0] >= '1' && line[0] <= '8';
                body.add(depth, "s2a_oled_line(" + (valid ? line : std::string("1")) + ", " +
                                    displayText(block, "TEXT") + ");");
            }
        } else if (type == "lights_neopixel_fill" || type == "lights_neopixel_set") {
            const std::string name = strip(block);
            const auto rgb = colourLevels(field(block, "COLOR"));
            const std::string colour = name + ".Color(" + std::to_string(rgb[0]) + ", " + std::to_string(rgb[1]) +
                                       ", " + std::to_string(rgb[2]) + ")";
            if (type == "lights_neopixel_fill") {
                body.add(depth, name + ".fill(" + colour + ");");
            } else {
                helpers_.insert("pixel");
                body.add(depth, "s2a_pixel(" + name + ", " + number(block, "INDEX") + ", " + colour + ");");
            }
            body.add(depth, name + ".show();");
        } else if (type == "lights_matrix_show") {
            const std::string target = matrix(block);
            const auto image = matrixImage(field(block, "IMAGE"));
            std::string bytes;
            for (const int row : image) {
                char hex[8];
                std::snprintf(hex, sizeof(hex), "0x%02X", row);
                bytes += (bytes.empty() ? "" : ", ") + std::string(hex);
            }
            body.add(depth, "{");
            body.add(depth + 1, "static const byte image[8] = {" + bytes + "};");
            body.add(depth + 1, "s2a_matrix_image(" + target + ", image);");
            body.add(depth, "}");
        } else if (type == "lights_matrix_dot") {
            const std::string target = matrix(block);
            body.add(depth, "s2a_matrix_dot(" + target + ", " + number(block, "X") + ", " + number(block, "Y") + ", " +
                                (field(block, "STATE") == "OFF" ? "false" : "true") + ");");
        } else if (type == "motors_servo_write") {
            const std::string label = pinLabel(block, "PIN");
            const std::string servo = "s2a_servo_" + label;
            use(esp32() ? "ESP32Servo.h" : "Servo.h", "Servo " + servo + ";", servo + ".attach(" + label + ");", label);
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
        } else if (type == "motors_stepper_turn") {
            helpers_.insert("stepper");
            std::string pins;
            for (const char* coil : {"IN1_PIN", "IN2_PIN", "IN3_PIN", "IN4_PIN"}) {
                pins += pin(block, coil, PinMode::Output) + ", ";
            }
            body.add(depth, "s2a_stepper(" + pins + number(block, "DEGREES") + ", " + number(block, "RPM") + ");");
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
            // The numbers given to its inputs, worked out where the call is.
            const auto& parameters = parameters_[id];
            std::vector<std::string> arguments;
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                arguments.push_back(number(block, "ARG" + std::to_string(i + 1)));
            }
            if (!tasks_) {
                std::string call = name->second + "(";
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    call += (i == 0 ? "" : ", ") + arguments[i];
                }
                body.add(depth, call + ");");
            } else if (inlining_.insert(id).second) {
                // Side by side, a wait inside the function must let the other scripts run:
                // its blocks are written here, inside the script. A local variable would not
                // survive a wait (the task returns), so each input of each call is a global.
                body.add(depth, "{ // " + oneLine(functionName(id)));
                Scope scope;
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    const std::string global = "s2a_" + parameters[i].second + "_" + std::to_string(++argumentGlobals_);
                    argumentDeclarations_.push_back("double " + global + " = 0; // input \"" +
                                                    oneLine(parameters[i].first) + "\" of " + oneLine(functionName(id)));
                    body.add(depth + 1, global + " = " + arguments[i] + ";");
                    scope.emplace(parameters[i].first, global);
                }
                scopes_.push_back(std::move(scope));
                const project::Stack& stack = definition->second->blocks;
                for (std::size_t i = 1; i < stack.size(); ++i) {
                    statement(body, stack[i], depth + 1);
                }
                scopes_.pop_back();
                body.add(depth, "}");
                inlining_.erase(id);
            }
        } else if (type == usercode::kCallBlock) {
            if (const std::string call = userCall(block, false); !call.empty()) {
                body.add(depth, call + ";");
            }
        } else if (type == usercode::kSetBlock) {
            const auto* found = userDeclaration(block, usercode::Declaration::Kind::Variable);
            if (found != nullptr && !found->readOnly) {
                if (found->text) { // a String: the number written as text, like the serial monitor does
                    helpers_.insert("text");
                    body.add(depth, found->name + " = s2a_text(" + number(block, "VALUE") + ");");
                } else {
                    body.add(depth, found->name + " = " + number(block, "VALUE") + ";");
                }
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
        for (const std::string& header : includes_) {
            out.push_back("#include <" + header + ">");
        }
        if (!includes_.empty()) {
            out.emplace_back("");
        }
        // The user's C++ file, beside the sketch: its functions and variables are known below.
        if (project_.userCode && project_.userCode->language == project::ProgramLanguage::Cpp) {
            out.push_back("#include \"" + std::string(kUserCodeFile) + "\" // My code");
            out.emplace_back("");
        }
        std::vector<std::string> globals = globals_;
        for (const auto& [label, pixels] : stripLengths_) {
            globals.push_back("Adafruit_NeoPixel s2a_strip_" + label + "(" + std::to_string(pixels) + ", " + label +
                              ", NEO_GRB + NEO_KHZ800);");
        }
        for (const std::string& rows : matrices_) {
            globals.push_back("byte " + rows + "[8]; // rows shown on the LED matrix");
        }
        if (!globals.empty()) {
            out.insert(out.end(), globals.begin(), globals.end());
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
        if (!argumentDeclarations_.empty()) {
            out.insert(out.end(), argumentDeclarations_.begin(), argumentDeclarations_.end());
            out.emplace_back("");
        }

        helper(out, "print",
               {"void s2a_print(double value) {",
                "  if (value == floor(value) && fabs(value) < 2000000000.0) {",
                "    Serial.println((long)value);", "  } else {", "    Serial.println(value, 2);", "  }", "}", "",
                "void s2a_print(const String& value) {", "  Serial.println(value);", "}"});
        helper(out, "wait",
               {"void s2a_wait(double seconds) {", "  if (seconds > 0) {",
                "    delay((unsigned long)(seconds * 1000.0));", "  }", "}"});
        helper(out, "tone",
               {"void s2a_tone(int pin, double frequency) {", "  if (frequency < 31) {", "    noTone(pin);",
                "  } else {", "    tone(pin, (unsigned int)min(frequency, 65535.0));", "  }", "}"});
        helper(out, "quiet",
               {"// A silent buzzer is let go: otherwise the flicker of LEDs on the same GND hums through it.",
                "void s2a_quiet(int pin) {", "  noTone(pin);", "  pinMode(pin, INPUT);", "}"});
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
        // Same steps as the Live firmware (stepMotor in firmware/s2a_live): two coils on at a time.
        helper(out, "stepper",
               {"void s2a_stepper(int in1, int in2, int in3, int in4, double degrees, double rpm) {",
                "  static const byte phases[4] = {0b1100, 0b0110, 0b0011, 0b1001};",
                "  const int pins[4] = {in1, in2, in3, in4};",
                "  const long steps = lround(degrees * 2048.0 / 360.0);",
                "  const unsigned long pause = 60000000UL / ((unsigned long)constrain(rpm, 1.0, 15.0) * 2048UL);",
                "  int phase = 0;",
                "  for (long done = 0; done < labs(steps); done++) {",
                "    phase = (phase + (steps > 0 ? 1 : 3)) % 4;",
                "    for (int i = 0; i < 4; i++) {",
                "      digitalWrite(pins[i], (phases[phase] >> (3 - i)) & 1 ? HIGH : LOW);",
                "    }",
                "    delay(pause / 1000);",
                "    delayMicroseconds(pause % 1000);",
                "  }",
                "  for (int i = 0; i < 4; i++) {",
                "    digitalWrite(pins[i], LOW); // coils off",
                "  }",
                "}"});
        helper(out, "text",
               {"String s2a_text(double value) {",
                "  if (value == floor(value) && fabs(value) < 2000000000.0) {", "    return String((long)value);",
                "  }", "  return String(value, 2);", "}", "", "String s2a_text(const String& value) {",
                "  return value;", "}"});
        helper(out, "phone_esp32",
               {"// Phone over Bluetooth: named \"S2A-xxxx\" like the Live program, pairing without a code.",
                "void s2a_phone_begin() {",
                "  uint8_t mac[6];",
                "  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);",
                "  char name[16];",
                "  snprintf(name, sizeof(name), \"S2A-%02X%02X\", mac[4], mac[5]);",
                "  s2a_phone.enableSSP(false, false);",
                "  s2a_phone.begin(name);",
                "}"});
        helper(out, "phone",
               {"// Messages of the phone (a Bluetooth terminal app).",
                "String s2a_phone_line;   // last message, until a block recognises it",
                "String s2a_phone_buffer; // message being received",
                "double s2a_phone_value = 0;",
                "",
                "// Lower case, accents and spaces around removed: \" Éteins\" is \"eteins\".",
                "String s2a_simple(const String& text) {",
                "  static const char* const kPlain = \"aaaaaaaceeeeiiiidnooooo/ouuuuypy\"; // U+00E0 to U+00FF",
                "  String result;",
                "  for (unsigned int i = 0; i < text.length(); i++) {",
                "    unsigned char c = text[i];",
                "    if (c == 0xC3 && i + 1 < text.length()) {",
                "      unsigned char next = text[++i];",
                "      if (next >= 0x80 && next <= 0x9F) {",
                "        next += 0x20; // capital letter",
                "      }",
                "      result += next >= 0xA0 && next <= 0xBF ? kPlain[next - 0xA0] : '?';",
                "    } else {",
                "      result += (char)tolower(c);",
                "    }",
                "  }",
                "  result.trim();",
                "  return result;",
                "}",
                "",
                "void s2a_phone_poll() {",
                "  while (s2a_phone.available() > 0) {",
                "    const char c = s2a_phone.read();",
                "    if (c == '\\r') {",
                "      continue;",
                "    }",
                "    if (c != '\\n') {",
                "      if (s2a_phone_buffer.length() < 60) {",
                "        s2a_phone_buffer += c;",
                "      }",
                "      continue;",
                "    }",
                "    s2a_phone_buffer.trim();",
                "    if (s2a_phone_buffer.length() > 0) {",
                "      s2a_phone_line = s2a_phone_buffer;",
                "      char* end = nullptr;",
                "      const double number = strtod(s2a_phone_line.c_str(), &end);",
                "      if (end != s2a_phone_line.c_str() && *end == '\\0') {",
                "        s2a_phone_value = number;",
                "      }",
                "    }",
                "    s2a_phone_buffer = \"\";",
                "  }",
                "}",
                "",
                "bool s2a_phone_received(const String& message) {",
                "  s2a_phone_poll();",
                "  if (s2a_phone_line.length() == 0 || s2a_simple(s2a_phone_line) != s2a_simple(message)) {",
                "    return false;",
                "  }",
                "  s2a_phone_line = \"\"; // true only once for each message",
                "  return true;",
                "}",
                "",
                "double s2a_phone_number() {",
                "  s2a_phone_poll();",
                "  return s2a_phone_value;",
                "}"});
        helper(out, "dht",
               {"// A DHT sensor does not always answer: keep its last good value.",
                "double s2a_dht_read(DHT& sensor, double* last, bool humidity) {",
                "  const float value = humidity ? sensor.readHumidity() : sensor.readTemperature();",
                "  if (!isnan(value)) {", "    last[humidity ? 1 : 0] = value;", "  }", "  return last[humidity ? 1 : 0];",
                "}"});
        helper(out, "lcd",
               {"// I2C LCD modules use the address 0x27 or 0x3F.", "void s2a_lcd_begin() {", "  Wire.begin();",
                "  Wire.beginTransmission(0x27);", "  const bool first = Wire.endTransmission() == 0;",
                "  s2a_lcd = new LiquidCrystal_I2C(first ? 0x27 : 0x3F, 16, 2);", "  s2a_lcd->init();",
                "  s2a_lcd->backlight();", "}", "", "void s2a_lcd_line(int row, const String& text) {",
                "  s2a_lcd->setCursor(0, row - 1);", "  for (unsigned int i = 0; i < 16; i++) {",
                "    s2a_lcd->print(i < text.length() ? text[i] : ' ');", "  }", "}"});
        helper(out, "lcd16",
               {"void s2a_lcd16_line(LiquidCrystal& lcd, int row, const String& text) {",
                "  lcd.setCursor(0, row - 1);", "  for (unsigned int i = 0; i < 16; i++) {",
                "    lcd.print(i < text.length() ? text[i] : ' ');", "  }", "}"});
        helper(out, "oled",
               {"void s2a_oled_line(int row, const String& text) {", "  char line[17];",
                "  for (unsigned int i = 0; i < 16; i++) {", "    line[i] = i < text.length() ? text[i] : ' ';", "  }",
                "  line[16] = '\\0';", "  s2a_oled.drawString(0, row - 1, line);", "}"});
        helper(out, "pixel",
               {"void s2a_pixel(Adafruit_NeoPixel& strip, double number, uint32_t colour) {",
                "  if (number >= 1 && number <= strip.numPixels()) {", "    strip.setPixelColor((int)number - 1, colour);",
                "  }", "}"});
        helper(out, "matrix",
               {"// MAX7219 8x8 LED matrix: one byte per row, the left column is the high bit.",
                "void s2a_matrix_send(int din, int clk, int cs, byte reg, byte data) {", "  digitalWrite(cs, LOW);",
                "  shiftOut(din, clk, MSBFIRST, reg);", "  shiftOut(din, clk, MSBFIRST, data);", "  digitalWrite(cs, HIGH);",
                "}", "", "// Settings sent again with each picture: a voltage dip (buzzer, motor) can reset the chip.",
                "void s2a_matrix_settings(int din, int clk, int cs) {", "  s2a_matrix_send(din, clk, cs, 0x0F, 0);",
                "  s2a_matrix_send(din, clk, cs, 0x09, 0);", "  s2a_matrix_send(din, clk, cs, 0x0A, 4);",
                "  s2a_matrix_send(din, clk, cs, 0x0B, 7);", "  s2a_matrix_send(din, clk, cs, 0x0C, 1);", "}", "",
                "void s2a_matrix_init(int din, int clk, int cs) {", "  digitalWrite(cs, HIGH);",
                "  s2a_matrix_settings(din, clk, cs);", "  for (byte row = 1; row <= 8; row++) {",
                "    s2a_matrix_send(din, clk, cs, row, 0);", "  }", "}", "",
                "void s2a_matrix_image(int din, int clk, int cs, byte* rows, const byte* image) {",
                "  s2a_matrix_settings(din, clk, cs);",
                "  for (byte row = 0; row < 8; row++) {", "    rows[row] = image[row];",
                "    s2a_matrix_send(din, clk, cs, row + 1, rows[row]);", "  }", "}", "",
                "void s2a_matrix_dot(int din, int clk, int cs, byte* rows, double x, double y, bool on) {",
                "  const int column = (int)x;", "  const int row = (int)y;",
                "  if (column < 1 || column > 8 || row < 1 || row > 8) {", "    return;", "  }",
                "  const byte bit = 0x80 >> (column - 1);",
                "  rows[row - 1] = on ? (rows[row - 1] | bit) : (rows[row - 1] & ~bit);",
                "  s2a_matrix_send(din, clk, cs, row, rows[row - 1]);", "}"});
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
            for (const auto& [signature, body] : functionBodies_) {
                out.push_back(signature + ";");
            }
            out.emplace_back("");
            for (const auto& [signature, body] : functionBodies_) {
                out.push_back(signature + " {");
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
            if (ownedPins_.count(label) == 0) { // a library sets the pins it drives
                out.push_back(std::string(kIndent) + "pinMode(" + label + ", " + modeName(mode) + ");");
            }
        }
        std::vector<std::string> inits = inits_;
        for (const auto& [label, pixels] : stripLengths_) {
            const std::string name = "s2a_strip_" + label;
            inits.push_back(name + ".begin();");
            inits.push_back(name + ".setBrightness(64); // full brightness dazzles and draws too much current");
            inits.push_back(name + ".show();");
        }
        inits.insert(inits.end(), matrixInits_.begin(), matrixInits_.end());
        for (const std::string& line : inits) {
            out.push_back(std::string(kIndent) + line);
        }
        if (esp32()) {
            out.push_back(std::string(kIndent) + "analogReadResolution(10); // 0-1023, as on the Arduino boards");
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
    std::vector<usercode::Declaration> userDeclarations_ = usercode::declarations(project_);
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
    std::vector<std::pair<std::string, Body>> functionBodies_;  ///< Signature and body (single-script programs).
    /// Function id -> its inputs: name in the blocks, C++ name of the parameter.
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> parameters_;
    using Scope = std::map<std::string, std::string>; ///< Input name -> C++ expression giving its number.
    std::vector<Scope> scopes_;                       ///< Inputs of the function being written (innermost last).
    int argumentGlobals_ = 0;
    std::vector<std::string> argumentDeclarations_; ///< Globals holding the inputs of calls written inside tasks.
    std::set<std::string> generatedFunctions_;
    std::set<std::string> inlining_; ///< Functions being written inside a script (no endless recursion).
    std::set<std::string> helpers_;
    std::set<std::string> includes_;    ///< Library headers.
    std::vector<std::string> globals_;  ///< Library objects, in order of first use.
    std::vector<std::string> inits_;    ///< Their set-up, in setup().
    std::set<std::string> ownedPins_;   ///< Pins set up by a library (no pinMode()).
    std::set<std::string> sharedPins_;  ///< Pins of other blocks than the buzzer ones.
    std::set<std::string> dhtSensors_;  ///< Pins of DHT sensors.
    std::map<std::string, int> stripLengths_; ///< NeoPixel pin -> number of LEDs.
    std::vector<std::string> matrices_;       ///< Row arrays of the LED matrices.
    std::vector<std::string> matrixInits_;
    int counters_ = 0;
};

} // namespace

std::array<int, 8> matrixImage(std::string_view name) {
    static const std::map<std::string_view, std::array<int, 8>> kImages = {
        {"HEART", {0x00, 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18}},
        {"SMILE", {0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C}},
        {"SAD", {0x3C, 0x42, 0xA5, 0x81, 0x99, 0xA5, 0x42, 0x3C}},
        {"ARROW_UP", {0x18, 0x3C, 0x7E, 0xFF, 0x18, 0x18, 0x18, 0x18}},
        {"ARROW_DOWN", {0x18, 0x18, 0x18, 0x18, 0xFF, 0x7E, 0x3C, 0x18}},
        {"ARROW_LEFT", {0x10, 0x30, 0x70, 0xFF, 0xFF, 0x70, 0x30, 0x10}},
        {"ARROW_RIGHT", {0x08, 0x0C, 0x0E, 0xFF, 0xFF, 0x0E, 0x0C, 0x08}},
        {"CHECK", {0x00, 0x01, 0x03, 0x06, 0x8C, 0xD8, 0x70, 0x20}},
        {"CROSS", {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81}},
        {"FULL", {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}},
    };
    const auto found = kImages.find(name);
    return found != kImages.end() ? found->second : std::array<int, 8>{};
}

std::string asciiText(std::string_view text) {
    // Accented Latin letters (UTF-8 C3 80..C3 BF) lose their accent; other characters become '?'.
    static constexpr std::string_view kLatin1 = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
                                                "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c >= 0x20 && c != 0x7F) {
                result += static_cast<char>(c);
            }
        } else if (c == 0xC3 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
                   static_cast<unsigned char>(text[i + 1]) <= 0xBF) {
            result += kLatin1[static_cast<unsigned char>(text[++i]) - 0x80];
        } else if (c == 0xC5 && i + 1 < text.size() && (text[i + 1] == '\x92' || text[i + 1] == '\x93')) {
            result += text[++i] == '\x92' ? "OE" : "oe"; // Œ œ
        } else if (c >= 0xC0) {
            result += '?';
            while (i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0U) == 0x80U) {
                ++i;
            }
        }
    }
    return result;
}

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

project::Project withCppUserCode(const project::Project& project, std::vector<int>* toCheck) {
    project::Project result = project;
    if (result.userCode && result.userCode->language != project::ProgramLanguage::Cpp &&
        result.userCode->language != project::ProgramLanguage::Blocks) {
        auto converted = convert::convert(result.userCode->code, result.userCode->language, project::ProgramLanguage::Cpp);
        result.userCode = project::UserCode{project::ProgramLanguage::Cpp, std::move(converted.code)};
        if (toCheck != nullptr) {
            *toCheck = std::move(converted.toCheck);
        }
    }
    return result;
}

std::string singleFileSketch(const project::Project& project) {
    const project::Project cpp = withCppUserCode(project);
    std::string source = generate(cpp).source;
    if (!cpp.userCode) {
        return source;
    }
    const std::string include = "#include \"" + std::string(kUserCodeFile) + "\" // My code";
    const auto at = source.find(include);
    if (at == std::string::npos) {
        return source;
    }
    std::string code = cpp.userCode->code;
    while (!code.empty() && (code.back() == '\n' || code.back() == ' ' || code.back() == '\r')) {
        code.pop_back();
    }
    source.replace(at, include.size(), "// ----- My code -----\n" + code + "\n\n// ----- Blocks -----");
    return source;
}

} // namespace s2a::codegen
