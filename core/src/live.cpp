#include "s2a/live.hpp"

#include "s2a/script.hpp"
#include "s2a/usercode.hpp"

#include "s2a/codegen.hpp"
#include "s2a/diagnostics.hpp"
#include "s2a/network.hpp"
#include "s2a/resources.hpp"
#include "s2a/validator.hpp"
#include "s2a/wiring.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>

namespace s2a::live {

namespace {

/// The pins of the first "Bluetooth module" block of the running scripts, if any.
std::optional<BluetoothModule> modulePins(const project::Project& project, const Board& board) {
    const BlockCatalog& catalog = BlockCatalog::builtin();
    std::optional<BluetoothModule> found;
    for (const project::Script& script : project.scripts) {
        const BlockDefinition* first = script.blocks.empty() ? nullptr : catalog.find(script.blocks.front().type);
        if (first == nullptr || first->shape != BlockShape::Hat) {
            continue;
        }
        project::forEachBlock(script.blocks, [&](const project::Block& block) {
            if (found || block.type != "phone_module") {
                return;
            }
            const BlockDefinition* definition = catalog.find(block.type);
            const auto number = [&](const char* name) {
                const auto field = block.fields.find(name);
                const BlockArg* arg = definition != nullptr ? definition->findArg(name) : nullptr;
                const Pin* pin = board.findPin(field != block.fields.end() && !field->second.empty()
                                                   ? field->second
                                                   : (arg != nullptr ? arg->defaultValue : std::string()));
                return pin != nullptr ? pin->number : -1;
            };
            if (number("RX_PIN") >= 0 && number("TX_PIN") >= 0) {
                found = BluetoothModule{number("RX_PIN"), number("TX_PIN"), 9600};
            }
        });
    }
    return found;
}

/// The blocks of the running scripts (loose blocks are ignored) that use one of `numbers`: through
/// a pin argument, the built-in LED, or a part wired to it (an I2C screen on SDA / SCL).
std::vector<std::string> blocksOnPins(const project::Project& project, const Board& board, Language language,
                                      std::initializer_list<int> numbers) {
    const BlockCatalog& catalog = BlockCatalog::builtin();
    const auto taken = [&](const Pin* pin) {
        return pin != nullptr && std::find(numbers.begin(), numbers.end(), pin->number) != numbers.end();
    };
    std::vector<std::string> blocks;
    const auto add = [&blocks](const std::string& id) {
        if (std::find(blocks.begin(), blocks.end(), id) == blocks.end()) {
            blocks.push_back(id);
        }
    };
    for (const project::Script& script : project.scripts) {
        const BlockDefinition* first = script.blocks.empty() ? nullptr : catalog.find(script.blocks.front().type);
        if (first == nullptr || first->shape != BlockShape::Hat) {
            continue;
        }
        project::forEachBlock(script.blocks, [&](const project::Block& block) {
            if (block.type == "phone_module") {
                return; // the module's own pins
            }
            if (block.type == "pins_builtin_led" && taken(board.findPin(board.builtinLed))) {
                add(block.id);
            }
            const BlockDefinition* definition = catalog.find(block.type);
            if (definition == nullptr) {
                return;
            }
            for (const BlockArg& arg : definition->args) {
                if (arg.kind != ArgKind::Pin) {
                    continue;
                }
                const auto field = block.fields.find(arg.name);
                if (taken(board.findPin(field != block.fields.end() && !field->second.empty() ? field->second
                                                                                               : arg.defaultValue))) {
                    add(block.id);
                }
            }
        });
    }
    for (const auto& component : wiring::plan(project, catalog, board, language).components) {
        if (component.kind == "bluetooth_module") {
            continue;
        }
        for (const auto& wire : component.connections) {
            if (taken(board.findPin(wire.boardPin))) {
                for (const auto& id : component.blockIds) {
                    add(id);
                }
            }
        }
    }
    return blocks;
}

using project::Block;
using project::Stack;
using std::chrono::milliseconds;

constexpr int kStepBudget = 500; ///< Blocks run per script and step before giving way.
constexpr std::string_view kDefine = "functions_define";
constexpr const char* kFunctionSlot = "#function"; ///< Frame slot of a function body (parent = its "define" hat).
constexpr std::size_t kMaxFrames = 200;

/// Ids of the phone blocks of `stack` (and of the blocks inside them).
void phoneBlocks(const Stack& stack, std::vector<std::string>& found) {
    for (const Block& block : stack) {
        if (block.type.rfind("phone_", 0) == 0) {
            found.push_back(block.id);
        }
        for (const auto& [name, input] : block.inputs) {
            if (input.block) {
                phoneBlocks(Stack{*input.block}, found);
            }
        }
        for (const auto& [name, inner] : block.statements) {
            phoneBlocks(inner, found);
        }
    }
}

std::optional<double> parseNumber(const std::string& text) {
    std::string trimmed;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            trimmed += c == ',' ? '.' : c;
        }
    }
    std::istringstream stream(trimmed);
    stream.imbue(std::locale::classic());
    double value = 0;
    stream >> value;
    if (trimmed.empty() || stream.fail() || !stream.eof() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

const Block* findIn(const Stack& stack, const std::string& id);

const Block* findInside(const Block& block, const std::string& id) {
    for (const auto& [name, input] : block.inputs) {
        if (input.block) {
            if (input.block->id == id) {
                return input.block.get();
            }
            if (const Block* found = findInside(*input.block, id)) {
                return found;
            }
        }
    }
    for (const auto& [name, stack] : block.statements) {
        if (const Block* found = findIn(stack, id)) {
            return found;
        }
    }
    return nullptr;
}

const Block* findIn(const Stack& stack, const std::string& id) {
    for (const Block& block : stack) {
        if (block.id == id) {
            return &block;
        }
        if (const Block* found = findInside(block, id)) {
            return found;
        }
    }
    return nullptr;
}

/// Value printed like the board does it (s2a_print): integers without decimals, else 2 decimals.
std::string formatValue(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    if (value == std::floor(value) && std::fabs(value) < 2e9) {
        stream << static_cast<long long>(value);
    } else {
        stream << std::fixed << std::setprecision(2) << value;
    }
    return stream.str();
}

} // namespace

// --- LiveLink ----------------------------------------------------------------------------

Status LiveLink::handshake(milliseconds timeout) {
    // A greeting proves that the firmware runs, but it may be an old one still waiting in the
    // port (sent before an upload ended, for example) while the board restarts: opening the
    // port resets most boards, and their bootloader swallows what is sent during the next
    // second or two. So the board must also answer a "?" before the blocks start.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto lastAsk = std::chrono::steady_clock::time_point::min();
    while (std::chrono::steady_clock::now() < deadline) {
        auto line = transport_.readLine(milliseconds(300));
        if (!line.ok()) {
            return line.error();
        }
        if (line.value() && *line.value() == kHello) {
            auto confirmed = confirm();
            if (!confirmed.ok()) {
                return confirmed.error();
            }
            if (confirmed.value()) {
                return success();
            }
            lastAsk = std::chrono::steady_clock::now();
            continue; // restarting: its own greeting will come
        }
        if (!line.value() && std::chrono::steady_clock::now() - lastAsk >= milliseconds(1000)) {
            // Silent board: it did not restart, or the bootloader swallowed the last "?".
            if (auto sent = transport_.writeLine("?"); !sent.ok()) {
                return sent.error();
            }
            lastAsk = std::chrono::steady_clock::now();
        }
    }
    return Error{ErrorCode::BoardNotResponding, "no S2A live greeting"};
}

Result<bool> LiveLink::confirm() {
    if (auto sent = transport_.writeLine("?"); !sent.ok()) {
        return sent.error();
    }
    const auto deadline = std::chrono::steady_clock::now() + milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        auto line = transport_.readLine(milliseconds(100));
        if (!line.ok()) {
            return line.error();
        }
        if (line.value() && *line.value() == kHello) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> LiveLink::request(const std::string& command, milliseconds timeout) {
    if (error_) {
        return std::nullopt;
    }
    if (auto sent = transport_.writeLine(command); !sent.ok()) {
        error_ = sent.error();
        return std::nullopt;
    }
    for (;;) {
        auto line = transport_.readLine(timeout);
        if (!line.ok()) {
            error_ = line.error();
            return std::nullopt;
        }
        if (!line.value()) {
            error_ = Error{ErrorCode::BoardNotResponding, "no answer to " + command};
            return std::nullopt;
        }
        if (*line.value() == kHello) {
            continue; // the board restarted: its greeting is not an answer
        }
        return line.value();
    }
}

void LiveLink::setMode(int pin, PinMode mode) {
    const char letter = mode == PinMode::Output ? 'O' : (mode == PinMode::InputPullup ? 'U' : 'I');
    (void)request("M " + std::to_string(pin) + " " + letter);
}

void LiveLink::digitalWrite(int pin, bool high) {
    (void)request("D " + std::to_string(pin) + (high ? " 1" : " 0"));
}

void LiveLink::analogWrite(int pin, int value) {
    (void)request("P " + std::to_string(pin) + " " + std::to_string(std::clamp(value, 0, 255)));
}

bool LiveLink::digitalRead(int pin) {
    const auto reply = request("R " + std::to_string(pin));
    return reply && reply->size() > 2 && reply->back() == '1';
}

int LiveLink::analogRead(int pin) {
    const auto reply = request("A " + std::to_string(pin));
    if (!reply) {
        return 0;
    }
    const auto space = reply->find_last_of(' ');
    const auto value = parseNumber(space == std::string::npos ? *reply : reply->substr(space + 1));
    return value ? static_cast<int>(*value) : 0;
}

void LiveLink::tone(int pin, int frequency, int milliseconds) {
    (void)request("T " + std::to_string(pin) + " " + std::to_string(std::clamp(frequency, 0, 65535)) + " " +
                  std::to_string(std::max(milliseconds, 0)));
}

void LiveLink::servoWrite(int pin, int angle) {
    (void)request("S " + std::to_string(pin) + " " + std::to_string(std::clamp(angle, 0, 180)));
}

double LiveLink::distance(int trigPin, int echoPin) {
    // The firmware answers in millimetres: "u <trig> <mm>".
    const auto reply = request("U " + std::to_string(trigPin) + " " + std::to_string(echoPin));
    if (!reply) {
        return 400;
    }
    const auto space = reply->find_last_of(' ');
    const auto value = parseNumber(space == std::string::npos ? *reply : reply->substr(space + 1));
    return value ? std::clamp(*value / 10.0, 0.0, 400.0) : 400;
}

std::optional<std::pair<double, double>> LiveLink::dht(int pin, int model) {
    // "h <pin> <tenths of °C> <tenths of %>"; the first reading waits for the sensor to wake up.
    const auto reply = request("H " + std::to_string(pin) + " " + std::to_string(model), milliseconds(3000));
    if (!reply) {
        return std::nullopt;
    }
    std::istringstream words(*reply);
    std::string kind;
    long number = 0;
    long temperature = -9999;
    long humidity = -9999;
    words >> kind >> number >> temperature >> humidity;
    if (words.fail() || temperature == -9999 || humidity == -9999) {
        return std::nullopt;
    }
    return std::pair{temperature / 10.0, humidity / 10.0};
}

namespace {
std::string screenText(const std::string& text) {
    std::string line = codegen::asciiText(text);
    return line.size() > 16 ? line.substr(0, 16) : line;
}
} // namespace

void LiveLink::lcdLine(int row, const std::string& text) {
    (void)request("L " + std::to_string(std::clamp(row, 0, 2)) + " " + screenText(text));
}

void LiveLink::lcd16Line(const std::array<int, 6>& pins, int row, const std::string& text) {
    std::string command = "C";
    for (const int pin : pins) {
        command += " " + std::to_string(pin);
    }
    (void)request(command + " " + std::to_string(std::clamp(row, 0, 2)) + " " + screenText(text));
}

void LiveLink::oledLine(int row, const std::string& text) {
    (void)request("O " + std::to_string(std::clamp(row, 0, 8)) + " " + screenText(text));
}

void LiveLink::pixels(int pin, int count, int index, int red, int green, int blue) {
    (void)request("N " + std::to_string(pin) + " " + std::to_string(std::clamp(count, 1, 100)) + " " +
                  std::to_string(std::max(index, 0)) + " " + std::to_string(std::clamp(red, 0, 255)) + " " +
                  std::to_string(std::clamp(green, 0, 255)) + " " + std::to_string(std::clamp(blue, 0, 255)));
}

void LiveLink::matrixRow(int din, int clk, int cs, int row, int bits) {
    (void)request("G " + std::to_string(din) + " " + std::to_string(clk) + " " + std::to_string(cs) + " " +
                  std::to_string(std::clamp(row, 1, 8)) + " " + std::to_string(bits & 0xFF));
}

void LiveLink::reset() {
    (void)request("X");
}

std::optional<std::string> LiveLink::phoneMessage() {
    const auto answer = request("B");
    if (!answer || answer->size() < 3 || answer->rfind("b ", 0) != 0) {
        return std::nullopt;
    }
    return answer->substr(2);
}

void LiveLink::phoneModule(int rx, int tx) {
    // Saved on the board too: the same link serves the Live mode over Bluetooth later.
    (void)request("J " + std::to_string(rx) + " " + std::to_string(tx) + " 9600", milliseconds(1000));
}

void LiveLink::phoneSend(const std::string& text) {
    std::string line;
    for (const char c : text) {
        line += c == '\n' || c == '\r' || c == '\t' ? ' ' : c;
    }
    if (line.size() > 100) { // the firmware reads lines of 120 bytes at most
        std::size_t end = 100;
        while (end > 0 && (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) {
            --end; // not in the middle of a UTF-8 character
        }
        line.resize(end);
    }
    (void)request("Y " + line);
}

std::string simpleText(std::string_view text) {
    static constexpr std::string_view kPlain = "aaaaaaaceeeeiiiidnooooo/ouuuuypy"; // U+00E0 to U+00FF
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size()) {
            auto next = static_cast<unsigned char>(text[++i]);
            if (next >= 0x80 && next <= 0x9F) {
                next += 0x20; // capital letter
            }
            result += next >= 0xA0 && next <= 0xBF ? kPlain[next - 0xA0] : '?';
        } else {
            result += static_cast<char>(std::tolower(c));
        }
    }
    const auto first = result.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    return result.substr(first, result.find_last_not_of(" \t") - first + 1);
}

Result<LiveLink::NetworkState> LiveLink::networkState() {
    const auto answer = request("I", milliseconds(1000));
    if (!answer) {
        return takeError().value_or(Error{ErrorCode::BoardNotResponding, "no answer to I"});
    }
    std::istringstream words(*answer);
    std::string kind;
    NetworkState state;
    words >> kind >> state.accessPoint >> state.accessPointAddress >> state.address;
    if (kind != "i" || state.accessPoint.empty()) {
        return Error{ErrorCode::BoardNotResponding, "unexpected answer to I: " + *answer};
    }
    if (state.address == "-") {
        state.address.clear();
    }
    return state;
}

Result<std::optional<BluetoothModule>> LiveLink::bluetoothModule() {
    const auto answer = request("K", milliseconds(1000));
    if (!answer) {
        return takeError().value_or(Error{ErrorCode::BoardNotResponding, "no answer to K"});
    }
    if (answer->rfind("! ", 0) == 0 || *answer == "k -") {
        return std::optional<BluetoothModule>(); // no module, or a board without one (ESP32)
    }
    std::istringstream words(*answer);
    std::string kind;
    BluetoothModule module;
    words >> kind >> module.rx >> module.tx >> module.baud;
    if (kind != "k" || words.fail()) {
        return Error{ErrorCode::BoardNotResponding, "unexpected answer to K: " + *answer};
    }
    return std::optional<BluetoothModule>(module);
}

Status LiveLink::setBluetoothModule(const std::optional<BluetoothModule>& module) {
    const std::string command = module ? "J " + std::to_string(module->rx) + " " + std::to_string(module->tx) + " " +
                                             std::to_string(module->baud)
                                       : std::string("J");
    const auto answer = request(command, milliseconds(1000));
    if (!answer || *answer != "ok") {
        return takeError().value_or(Error{ErrorCode::BoardNotResponding, "unexpected answer to J: " + answer.value_or("")});
    }
    return success();
}

std::string bluetoothModuleProblem(const Board& board, const BluetoothModule& module) {
    return s2a::bluetoothModuleProblem(board, module.rx, module.tx, module.baud);
}

Status LiveLink::joinNetwork(const std::string& name, const std::string& password) {
    const auto clean = [](std::string text) { // one line, the tab separates the name and the password
        text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == '\n' || c == '\r' || c == '\t'; }),
                   text.end());
        return text;
    };
    const auto answer = request("W " + clean(name) + "\t" + clean(password), milliseconds(3000));
    if (!answer || *answer != "ok") {
        return takeError().value_or(Error{ErrorCode::BoardNotResponding, "no answer to W"});
    }
    return success();
}

std::optional<Error> LiveLink::takeError() {
    auto error = std::move(error_);
    error_.reset();
    return error;
}

// --- Interpreter -------------------------------------------------------------------------

Interpreter::Interpreter(project::Project project, const BlockCatalog& catalog, const Board& board, Pins& pins,
                         UserCode* userCode)
    : project_(std::move(project)), catalog_(catalog), board_(board), pins_(pins), userCode_(userCode) {
    for (const project::Variable& variable : project_.variables) {
        variables_[variable.id] = variable.initialValue;
    }
    readUserDeclarations();
    start();
}

void Interpreter::readUserDeclarations() {
    userParameters_.clear();
    for (const auto& declaration : usercode::declarations(project_)) {
        if (declaration.kind == usercode::Declaration::Kind::Function) {
            userParameters_[declaration.name] = declaration.parameters.size();
        }
    }
}

std::vector<double> Interpreter::userArguments(const Block& block) {
    std::vector<double> arguments;
    const auto found = userParameters_.find(field(block, std::string(usercode::kNameField)));
    const std::size_t count = found != userParameters_.end() ? found->second : 0;
    for (std::size_t i = 0; i < count; ++i) {
        arguments.push_back(number(block, "ARG" + std::to_string(i + 1)));
    }
    return arguments;
}

void Interpreter::start() {
    threads_.clear();
    for (const project::Script& script : project_.scripts) {
        if (script.blocks.empty()) {
            continue;
        }
        const BlockDefinition* definition = catalog_.find(script.blocks.front().type);
        if (definition == nullptr || definition->shape != BlockShape::Hat) {
            continue; // loose blocks do not run
        }
        if (script.blocks.front().type == kDefine) {
            continue; // a function runs when it is called
        }
        Thread thread;
        thread.hat = script.blocks.front().id;
        thread.frames.push_back({&script.blocks, 1, {}, {}, {}, 0});
        threads_.push_back(std::move(thread));
    }
}

void Interpreter::step(Clock now) {
    for (Thread& thread : threads_) {
        if (!thread.done && thread.wakeAt <= now) {
            run(thread, now);
        }
    }
}

bool Interpreter::finished() const {
    return std::all_of(threads_.begin(), threads_.end(), [](const Thread& thread) { return thread.done; });
}

std::vector<std::string> Interpreter::runningBlocks() const {
    std::vector<std::string> ids;
    for (const Thread& thread : threads_) {
        if (!thread.done && !thread.current.empty()) {
            ids.push_back(thread.current);
        }
    }
    return ids;
}

std::string Interpreter::field(const Block& block, const std::string& name) const {
    if (const auto found = block.fields.find(name); found != block.fields.end() && !found->second.empty()) {
        return found->second;
    }
    const BlockDefinition* definition = catalog_.find(block.type);
    const BlockArg* arg = definition != nullptr ? definition->findArg(name) : nullptr;
    return arg != nullptr ? arg->defaultValue : std::string{};
}

void Interpreter::record(int pin, double value) {
    const Pin* found = board_.findPin(pin);
    readings_[found != nullptr ? found->label : std::to_string(pin)] = value;
}

std::vector<std::string> Interpreter::takeOutput() {
    std::vector<std::string> lines;
    lines.swap(output_);
    return lines;
}

int Interpreter::pin(const Block& block, const std::string& name, PinMode mode) {
    const Pin* found = board_.findPin(field(block, name));
    if (found == nullptr) {
        return -1; // the validator reports it before Live starts
    }
    auto it = modes_.find(found->number);
    // Reading a pin set as an input with pull-up keeps the pull-up.
    const bool keep = it != modes_.end() && mode == PinMode::Input && it->second == PinMode::InputPullup;
    if (!keep && (it == modes_.end() || it->second != mode)) {
        pins_.setMode(found->number, mode);
        modes_[found->number] = mode;
    }
    return found->number;
}

void Interpreter::pollPhone() {
    if (auto message = pins_.phoneMessage()) {
        phoneLine_ = std::move(*message);
        if (const auto value = parseNumber(phoneLine_)) {
            phoneNumber_ = *value;
        }
    }
}

std::string Interpreter::text(const Block& block, const std::string& name) {
    const auto input = block.inputs.find(name);
    if (input != block.inputs.end() && input->second.block) {
        return formatValue(evaluate(*input->second.block));
    }
    return input != block.inputs.end() ? input->second.value : field(block, name);
}

int Interpreter::pinNumber(const Block& block, const std::string& name) const {
    const Pin* found = board_.findPin(field(block, name));
    return found != nullptr ? found->number : -1;
}

double Interpreter::number(const Block& block, const std::string& name) {
    if (const auto input = block.inputs.find(name); input != block.inputs.end()) {
        if (input->second.block) {
            return evaluate(*input->second.block);
        }
        return parseNumber(input->second.value).value_or(0);
    }
    return parseNumber(field(block, name)).value_or(0);
}

bool Interpreter::condition(const Block& block, const std::string& name) {
    const auto input = block.inputs.find(name);
    return input != block.inputs.end() && input->second.block && evaluate(*input->second.block) != 0;
}

double Interpreter::evaluate(const Block& block) {
    const std::string& type = block.type;
    const auto both = [&](const char* a, const char* b) { return std::pair{number(block, a), number(block, b)}; };
    if (type == "operators_add") {
        const auto [a, b] = both("NUM1", "NUM2");
        return a + b;
    }
    if (type == "operators_subtract") {
        const auto [a, b] = both("NUM1", "NUM2");
        return a - b;
    }
    if (type == "operators_multiply") {
        const auto [a, b] = both("NUM1", "NUM2");
        return a * b;
    }
    if (type == "operators_divide") {
        const auto [a, b] = both("NUM1", "NUM2");
        return b == 0 ? 0 : a / b;
    }
    if (type == "operators_lt") {
        const auto [a, b] = both("NUM1", "NUM2");
        return a < b ? 1 : 0;
    }
    if (type == "operators_gt") {
        const auto [a, b] = both("NUM1", "NUM2");
        return a > b ? 1 : 0;
    }
    if (type == "operators_equals") {
        const auto [a, b] = both("NUM1", "NUM2");
        return a == b ? 1 : 0;
    }
    if (type == "operators_and") {
        return condition(block, "OPERAND1") && condition(block, "OPERAND2") ? 1 : 0;
    }
    if (type == "operators_or") {
        return condition(block, "OPERAND1") || condition(block, "OPERAND2") ? 1 : 0;
    }
    if (type == "operators_not") {
        return condition(block, "OPERAND") ? 0 : 1;
    }
    if (type == "operators_random") {
        const auto [a, b] = both("FROM", "TO");
        const auto low = static_cast<long>(std::min(a, b));
        const auto high = static_cast<long>(std::max(a, b));
        return static_cast<double>(std::uniform_int_distribution<long>(low, high)(random_));
    }
    if (type == "operators_map") {
        const double value = number(block, "VALUE");
        const double fromLow = number(block, "FROM_LOW");
        const double fromHigh = number(block, "FROM_HIGH");
        const double toLow = number(block, "TO_LOW");
        const double toHigh = number(block, "TO_HIGH");
        return fromHigh == fromLow ? toLow : toLow + (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow);
    }
    if (type == "pins_digital_read") {
        const int number = pin(block, "PIN", PinMode::Input);
        if (number < 0) {
            return 0;
        }
        const bool high = pins_.digitalRead(number);
        record(number, high ? 1 : 0);
        return high == (field(block, "STATE") != "LOW") ? 1 : 0;
    }
    if (type == "pins_analog_read") {
        const int number = pin(block, "PIN", PinMode::Input);
        if (number < 0) {
            return 0;
        }
        const int value = pins_.analogRead(number);
        record(number, value);
        return value;
    }
    if (type == "phone_received") {
        pollPhone();
        if (phoneLine_.empty() || simpleText(phoneLine_) != simpleText(text(block, "MESSAGE"))) {
            return 0;
        }
        phoneLine_.clear(); // true only once for each message
        return 1;
    }
    if (type == "phone_number") {
        pollPhone();
        return phoneNumber_;
    }
    if (type == "sensors_button_pressed") {
        const int number = pin(block, "PIN", PinMode::InputPullup);
        if (number < 0) {
            return 0;
        }
        const bool pressed = !pins_.digitalRead(number); // wired to GND: LOW when pressed
        record(number, pressed ? 1 : 0);
        return pressed ? 1 : 0;
    }
    if (type == "sensors_potentiometer" || type == "sensors_light") {
        const int number = pin(block, "PIN", PinMode::Input);
        if (number < 0) {
            return 0;
        }
        const double percent = pins_.analogRead(number) * 100.0 / 1023.0;
        record(number, percent);
        return percent;
    }
    if (type == "sensors_distance") {
        const int trig = pinNumber(block, "TRIG_PIN");
        const int echo = pinNumber(block, "ECHO_PIN");
        if (trig < 0 || echo < 0) {
            return 400;
        }
        modes_[trig] = PinMode::Output; // the firmware sets both pins itself
        modes_[echo] = PinMode::Input;
        const double centimetres = pins_.distance(trig, echo);
        record(echo, centimetres);
        return centimetres;
    }
    if (type == "sound_note") {
        return parseNumber(field(block, "NOTE")).value_or(0);
    }
    if (type == "sensors_temperature" || type == "sensors_humidity") {
        const int number = pinNumber(block, "PIN");
        if (number < 0) {
            return 0;
        }
        modes_.erase(number); // the firmware sets the pin of the sensor
        if (const auto measured = pins_.dht(number, field(block, "MODEL") == "DHT22" ? 22 : 11)) {
            dht_[number] = *measured; // a sensor that does not answer keeps its last values
        }
        const auto& values = dht_[number];
        const Pin* found = board_.findPin(number);
        const std::string label = found != nullptr ? found->label : std::to_string(number);
        readings_[label + " (°C)"] = values.first;
        readings_[label + " (%)"] = values.second;
        return type == "sensors_humidity" ? values.second : values.first;
    }
    if (type == usercode::kValueBlock || type == usercode::kGetBlock) {
        // The validator checked that the name is in the code before Live mode started.
        if (userCode_ == nullptr || userCode_->failed()) {
            return 0;
        }
        const std::string name = field(block, std::string(usercode::kNameField));
        return type == usercode::kGetBlock ? userCode_->get(name) : userCode_->call(name, userArguments(block));
    }
    if (type == "variables_get") {
        const auto found = variables_.find(field(block, "VARIABLE"));
        return found != variables_.end() ? found->second : 0;
    }
    return 0;
}

void Interpreter::run(Thread& thread, Clock now) {
    for (int budget = kStepBudget; budget > 0; --budget) {
        if (thread.frames.empty()) {
            thread.done = true;
            thread.current.clear();
            return;
        }
        Frame& frame = thread.frames.back();
        if (frame.index >= frame.stack->size()) {
            if (!frame.owner.empty() && (frame.remaining < 0 || --frame.remaining > 0)) {
                frame.index = 0;
                return; // a loop gives way at the end of each turn
            }
            thread.frames.pop_back();
            continue;
        }
        const Block& block = (*frame.stack)[frame.index];
        thread.current = block.id;
        ++frame.index; // before pushing frames: `frame` may move
        const std::string& type = block.type;
        const auto enter = [&](const char* name, long turns, bool loop) {
            const auto stack = block.statements.find(name);
            static const Stack kEmpty;
            const Stack* inner = stack != block.statements.end() ? &stack->second : &kEmpty;
            if (inner->empty() && !loop) {
                return;
            }
            thread.frames.push_back({inner, 0, loop ? block.id : std::string(), block.id, name, turns});
        };

        if (type == "pins_builtin_led") {
            if (board_.builtinLed >= 0) {
                if (auto it = modes_.find(board_.builtinLed); it == modes_.end() || it->second != PinMode::Output) {
                    pins_.setMode(board_.builtinLed, PinMode::Output);
                    modes_[board_.builtinLed] = PinMode::Output;
                }
                const bool lit = field(block, "STATE") != "OFF";
                pins_.digitalWrite(board_.builtinLed, board_.builtinLedActiveLow ? !lit : lit);
            }
        } else if (type == "pins_digital_write") {
            if (const int number = pin(block, "PIN", PinMode::Output); number >= 0) {
                pins_.digitalWrite(number, field(block, "STATE") != "LOW");
            }
        } else if (type == "pins_set_mode") {
            if (const Pin* target = board_.findPin(field(block, "PIN")); target != nullptr) {
                const PinMode mode = field(block, "MODE") == "INPUT" ? PinMode::Input : PinMode::InputPullup;
                pins_.setMode(target->number, mode); // always: the block may remove the pull-up
                modes_[target->number] = mode;
            }
        } else if (type == "serial_print") {
            const auto input = block.inputs.find("VALUE");
            if (input != block.inputs.end() && input->second.block) {
                output_.push_back(formatValue(evaluate(*input->second.block)));
            } else {
                output_.push_back(input != block.inputs.end() ? input->second.value : field(block, "VALUE"));
            }
        } else if (type == "phone_send") {
            pins_.phoneSend(text(block, "VALUE"));
        } else if (type == "phone_module") {
            const int rx = pinNumber(block, "RX_PIN");
            const int tx = pinNumber(block, "TX_PIN");
            if (rx >= 0 && tx >= 0) {
                pins_.phoneModule(rx, tx);
            }
        } else if (type == "pins_analog_write") {
            const double value = number(block, "VALUE");
            if (const int number = pin(block, "PIN", PinMode::Output); number >= 0) {
                pins_.analogWrite(number, static_cast<int>(std::clamp(value, 0.0, 255.0)));
            }
        } else if (type == "control_wait") {
            const double seconds = number(block, "SECONDS");
            thread.wakeAt = now + milliseconds(static_cast<long long>(std::max(0.0, seconds) * 1000));
            return;
        } else if (type == "lights_rgb_color" || type == "lights_rgb_mix") {
            std::array<int, 3> levels = codegen::colourLevels(field(block, "COLOR"));
            if (type == "lights_rgb_mix") {
                const char* const names[] = {"RED", "GREEN", "BLUE"};
                for (std::size_t i = 0; i < levels.size(); ++i) {
                    levels[i] = static_cast<int>(std::clamp(number(block, names[i]), 0.0, 255.0));
                }
            }
            const char* const pins[] = {"RED_PIN", "GREEN_PIN", "BLUE_PIN"};
            for (std::size_t i = 0; i < levels.size(); ++i) {
                if (const int number = pin(block, pins[i], PinMode::Output); number >= 0) {
                    pins_.analogWrite(number, levels[i]);
                }
            }
        } else if (type == "sound_play_tone" || type == "sound_start_tone") {
            const double frequency = number(block, "FREQUENCY");
            const double seconds = type == "sound_play_tone" ? std::max(0.0, number(block, "SECONDS")) : 0;
            if (const int number = pinNumber(block, "PIN"); number >= 0) {
                modes_.erase(number); // tone() sets the pin, the firmware lets it go when the sound ends
                const int hertz = frequency < 31 ? 0 : static_cast<int>(std::min(frequency, 65535.0));
                const auto duration = static_cast<int>(std::min(seconds * 1000, 3600000.0));
                if (type == "sound_play_tone" && duration == 0) {
                    pins_.tone(number, 0, 0);
                } else {
                    pins_.tone(number, hertz, duration); // the board stops the sound itself
                }
            }
            if (type == "sound_play_tone") {
                thread.wakeAt = now + milliseconds(static_cast<long long>(seconds * 1000));
                return;
            }
        } else if (type == "sound_stop") {
            if (const int number = pinNumber(block, "PIN"); number >= 0) {
                pins_.tone(number, 0, 0);
            }
        } else if (type == "display_lcd_print") {
            pins_.lcdLine(field(block, "LINE") == "2" ? 2 : 1, text(block, "TEXT"));
        } else if (type == "display_lcd_clear") {
            pins_.lcdLine(0, {});
        } else if (type == "display_lcd16_print" || type == "display_lcd16_clear") {
            std::array<int, 6> wiring{};
            bool wired = true;
            const char* const names[] = {"RS_PIN", "E_PIN", "D4_PIN", "D5_PIN", "D6_PIN", "D7_PIN"};
            for (std::size_t i = 0; i < wiring.size(); ++i) {
                wiring[i] = pinNumber(block, names[i]);
                wired = wired && wiring[i] >= 0;
                modes_.erase(wiring[i]); // the screen owns its pins
            }
            if (wired) {
                const bool clear = type == "display_lcd16_clear";
                pins_.lcd16Line(wiring, clear ? 0 : (field(block, "LINE") == "2" ? 2 : 1),
                                clear ? std::string() : text(block, "TEXT"));
            }
        } else if (type == "display_oled_print") {
            const int line = static_cast<int>(parseNumber(field(block, "LINE")).value_or(1));
            pins_.oledLine(std::clamp(line, 1, 8), text(block, "TEXT"));
        } else if (type == "display_oled_clear") {
            pins_.oledLine(0, {});
        } else if (type == "lights_neopixel_fill" || type == "lights_neopixel_set") {
            const auto count = static_cast<int>(std::clamp(number(block, "COUNT"), 1.0, 100.0));
            const int index = type == "lights_neopixel_set" ? static_cast<int>(number(block, "INDEX")) : 0;
            if (const int number = pinNumber(block, "PIN"); number >= 0) {
                modes_.erase(number);
                int& length = stripLengths_[number];
                length = std::max(length, count); // as in the sketch: the longest strip of this pin
                const auto rgb = codegen::colourLevels(field(block, "COLOR"));
                if (index == 0 || (index >= 1 && index <= length)) {
                    pins_.pixels(number, length, index, rgb[0], rgb[1], rgb[2]);
                }
            }
        } else if (type == "lights_matrix_show" || type == "lights_matrix_dot") {
            const int din = pin(block, "DIN_PIN", PinMode::Output);
            const int clk = pin(block, "CLK_PIN", PinMode::Output);
            const int cs = pin(block, "CS_PIN", PinMode::Output);
            if (din >= 0 && clk >= 0 && cs >= 0) {
                auto& rows = matrices_[std::to_string(din) + " " + std::to_string(clk) + " " + std::to_string(cs)];
                if (type == "lights_matrix_show") {
                    rows = codegen::matrixImage(field(block, "IMAGE"));
                    for (int row = 0; row < 8; ++row) {
                        pins_.matrixRow(din, clk, cs, row + 1, rows[row]);
                    }
                } else {
                    const auto column = static_cast<int>(number(block, "X"));
                    const auto row = static_cast<int>(number(block, "Y"));
                    if (column >= 1 && column <= 8 && row >= 1 && row <= 8) {
                        const int bit = 0x80 >> (column - 1);
                        rows[row - 1] = field(block, "STATE") == "OFF" ? (rows[row - 1] & ~bit) : (rows[row - 1] | bit);
                        pins_.matrixRow(din, clk, cs, row, rows[row - 1]);
                    }
                }
            }
        } else if (type == "motors_servo_write") {
            const double angle = number(block, "ANGLE");
            if (const int number = pinNumber(block, "PIN"); number >= 0) {
                modes_.erase(number); // the servo owns the pin now
                pins_.servoWrite(number, static_cast<int>(std::clamp(angle, 0.0, 180.0)));
            }
        } else if (type == "motors_dc_run" || type == "motors_dc_stop") {
            const bool run = type == "motors_dc_run";
            const double speed = run ? std::clamp(number(block, "SPEED"), 0.0, 100.0) : 0;
            const bool backward = field(block, "DIRECTION") == "BACKWARD";
            const int en = pin(block, "EN_PIN", PinMode::Output);
            const int in1 = pin(block, "IN1_PIN", PinMode::Output);
            const int in2 = pin(block, "IN2_PIN", PinMode::Output);
            if (en >= 0 && in1 >= 0 && in2 >= 0) {
                pins_.digitalWrite(in1, run && !backward);
                pins_.digitalWrite(in2, run && backward);
                pins_.analogWrite(en, static_cast<int>(speed * 2.55));
            }
        } else if (type == "control_wait_until") {
            if (!condition(block, "CONDITION")) {
                --thread.frames.back().index; // check again at the next step
                return;
            }
        } else if (type == "control_repeat") {
            const auto turns = static_cast<long>(number(block, "TIMES"));
            if (turns > 0) {
                enter("SUBSTACK", turns, true);
            }
        } else if (type == "control_forever") {
            enter("SUBSTACK", -1, true);
        } else if (type == "control_if") {
            if (condition(block, "CONDITION")) {
                enter("SUBSTACK", 0, false);
            }
        } else if (type == "control_if_else") {
            enter(condition(block, "CONDITION") ? "SUBSTACK" : "SUBSTACK2", 0, false);
        } else if (type == "functions_call") {
            const std::string id = field(block, "FUNCTION");
            for (const project::Script& script : project_.scripts) {
                if (!script.blocks.empty() && script.blocks.front().type == kDefine &&
                    field(script.blocks.front(), "FUNCTION") == id) {
                    // Like the mouth of an if: the blocks of the function, then back after the call.
                    // The validator refuses recursion; the limit only protects from a mistake.
                    if (thread.frames.size() < kMaxFrames) {
                        thread.frames.push_back({&script.blocks, 1, {}, script.blocks.front().id, kFunctionSlot, 0});
                    }
                    break;
                }
            }
        } else if (type == usercode::kCallBlock || type == usercode::kSetBlock) {
            if (userCode_ != nullptr && !userCode_->failed()) {
                const std::string name = field(block, std::string(usercode::kNameField));
                if (type == usercode::kSetBlock) {
                    userCode_->set(name, number(block, "VALUE"));
                } else {
                    (void)userCode_->call(name, userArguments(block));
                }
            }
        } else if (type == "variables_set" || type == "variables_change") {
            const std::string id = field(block, "VARIABLE");
            if (auto found = variables_.find(id); found != variables_.end()) {
                const double value = number(block, "VALUE");
                found->second = type == "variables_set" ? value : found->second + value;
            }
        }
    }
}

bool Interpreter::restore(Thread& thread, const std::vector<Frame>& old) {
    // Frames remember where their stack lives (parent block + mouth, or the script itself).
    std::vector<Frame> frames;
    for (const Frame& frame : old) {
        const Stack* stack = nullptr;
        if (frame.parent.empty()) {
            for (const project::Script& script : project_.scripts) {
                if (!script.blocks.empty() && script.blocks.front().id == thread.hat) {
                    stack = &script.blocks;
                }
            }
        } else {
            const Block* parent = nullptr;
            for (const project::Script& script : project_.scripts) {
                if (const Block* found = findIn(script.blocks, frame.parent)) {
                    parent = found;
                    break;
                }
            }
            if (frame.slot == kFunctionSlot) {
                for (const project::Script& script : project_.scripts) {
                    if (!script.blocks.empty() && script.blocks.front().id == frame.parent) {
                        stack = &script.blocks;
                    }
                }
            } else if (parent != nullptr) {
                if (const auto it = parent->statements.find(frame.slot); it != parent->statements.end()) {
                    stack = &it->second;
                }
            }
        }
        if (stack == nullptr) {
            return false;
        }
        Frame copy = frame;
        copy.stack = stack;
        copy.index = std::min(frame.index, stack->size());
        frames.push_back(copy);
    }
    thread.frames = std::move(frames);
    return true;
}

void Interpreter::update(project::Project project) {
    // Old frames point into the old project: keep it alive until they are converted.
    std::vector<Thread> old = std::move(threads_);
    project::Project previous = std::move(project_);
    project_ = std::move(project);
    for (const project::Variable& variable : project_.variables) {
        variables_.emplace(variable.id, variable.initialValue); // existing values are kept
    }
    readUserDeclarations();
    start(); // one fresh thread per hat
    for (Thread& thread : threads_) {
        const auto before = std::find_if(old.begin(), old.end(), [&](const Thread& t) { return t.hat == thread.hat; });
        if (before == old.end()) {
            continue;
        }
        Thread restored = *before;
        if (restore(restored, before->frames)) {
            thread = std::move(restored);
        }
    }
    (void)previous;
}

// --- Session -----------------------------------------------------------------------------

Session::Session(project::Project project, const Board& board, std::string port, Language language, Opener opener,
                 Flasher flasher)
    : board_(board), port_(std::move(port)), language_(language), opener_(std::move(opener)),
      flasher_(std::move(flasher)) {
    snapshot_.message = tr("live.connecting", language_, {{"port", port_}});
    thread_ = std::thread([this, project = std::move(project)]() mutable { run(std::move(project)); });
}

Session::~Session() {
    stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Session::update(project::Project project) {
    const std::lock_guard<std::mutex> lock(mutex_);
    pending_ = std::move(project);
}

void Session::stop() {
    stopping_ = true;
}

Session::Snapshot Session::snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool Session::active() const {
    const State state = snapshot().state;
    return state == State::Connecting || state == State::Flashing || state == State::Running;
}

void Session::set(State state, std::string message, std::string details) {
    const std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.state = state;
    snapshot_.message = std::move(message);
    snapshot_.details = std::move(details);
    if (state != State::Running) {
        snapshot_.runningBlocks.clear();
    }
}

void Session::run(project::Project project) {
    if (project.language == project::ProgramLanguage::Cpp) {
        set(State::Failed, tr("text.live_cpp", language_));
        return;
    }
    const bool scripted = script::isScript(project.language);
    // "My code" in C++ is compiled for the board: the Live mode cannot run it.
    if (!scripted && project.userCode && project.userCode->language == project::ProgramLanguage::Cpp &&
        usercode::usesUserCode(project)) {
        set(State::Failed, tr("text.usercode_upload_only", language_));
        return;
    }
    // The blocks are checked first, as for an upload (a typed program is checked as it runs).
    const auto issues = scripted ? std::vector<validation::Issue>{} : validation::validate(project, BlockCatalog::builtin(), &board_, language_);
    if (validation::hasErrors(issues)) {
        std::vector<std::string> blocks;
        std::string first;
        for (const auto& issue : issues) {
            if (issue.severity == validation::Severity::Error) {
                if (first.empty()) {
                    first = validation::describe(issue, language_);
                }
                blocks.push_back(issue.blockId);
            }
        }
        set(State::Failed, first);
        const std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.problemBlocks = std::move(blocks);
        return;
    }

    // Over Bluetooth the computer takes the board's only Bluetooth connection: no room for the phone.
    if (!scripted && (network::bluetoothAddress(port_) || (network::isWireless(port_) && !network::wifiAddress(port_)))) {
        std::vector<std::string> blocks;
        for (const auto& script : project.scripts) {
            phoneBlocks(script.blocks, blocks);
        }
        if (!blocks.empty()) {
            set(State::Failed, tr("live.phone_bluetooth", language_));
            const std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.problemBlocks = std::move(blocks);
            return;
        }
    }

    const auto fail = [this](const Error& error, std::string details = {}) {
        set(State::Failed, tr(errorMessageKey(error.code), language_), error.detail + (details.empty() ? "" : "\n" + details));
    };

    auto transport = opener_(port_);
    if (!transport.ok()) {
        fail(transport.error());
        return;
    }
    std::unique_ptr<serial::LineTransport> connection = std::move(transport).value();
    // No firmware yet answers within a few seconds: the reset by the port lasts 1 to 2 s.
    if (!LiveLink(*connection).handshake(milliseconds(3500)).ok()) {
        if (stopping_) {
            set(State::Stopped, tr("live.stopped", language_));
            return;
        }
        // No S2A live firmware on the board yet: upload it, then connect again.
        connection.reset();
        if (!flasher_) {
            set(State::Failed, tr(network::bluetoothAddress(port_) ? "live.bluetooth_no_firmware" : "live.wifi_no_firmware",
                                  language_));
            return;
        }
        set(State::Flashing, tr("live.flashing", language_, {{"board", board_.name}}));
        std::string details;
        if (const auto flashed = flasher_(details); !flashed.ok()) {
            fail(flashed.error(), details);
            return;
        }
        transport = opener_(port_);
        if (!transport.ok()) {
            fail(transport.error());
            return;
        }
        connection = std::move(transport).value();
        if (const auto ready = LiveLink(*connection).handshake(); !ready.ok()) {
            fail(ready.error());
            return;
        }
    }

    LiveLink link(*connection);
    if (scripted) {
        runScript(project, link);
        return;
    }
    // The pins of an HC-05 / HC-06 module carry the link: the blocks must leave them alone.
    if (board_.bluetoothModule) {
        // The "Bluetooth module" block moves the link to its pins; otherwise it stays where it was saved.
        auto module = link.bluetoothModule();
        if (const auto wanted = modulePins(project, board_); wanted && module.ok()) {
            module = std::optional<BluetoothModule>(*wanted);
        }
        if (module.ok() && module.value()) {
            const BluetoothModule& pins = *module.value();
            std::vector<std::string> blocks = blocksOnPins(project, board_, language_, {pins.rx, pins.tx});
            if (!blocks.empty()) {
                const auto label = [this](int number) {
                    const Pin* pin = board_.findPin(number);
                    return pin != nullptr ? pin->label : std::to_string(number);
                };
                link.reset();
                set(State::Failed, tr("live.module_pins", language_, {{"rx", label(pins.rx)}, {"tx", label(pins.tx)}}));
                const std::lock_guard<std::mutex> lock(mutex_);
                snapshot_.problemBlocks = std::move(blocks);
                return;
            }
        }
    }
    // "My code" in Python or JavaScript: loaded once (its top-level statements run), then the
    // blocks call its functions.
    std::optional<Error> linkError;
    script::Host host = scriptHost(link, linkError);
    std::unique_ptr<script::Module> module;
    if (project.userCode && script::isScript(project.userCode->language)) {
        std::optional<script::ScriptError> error;
        module = script::Module::load(project.userCode->language, project.userCode->code, host, language_, error);
        if (!module) {
            link.reset();
            if (linkError) {
                fail(*linkError);
            } else if (error) {
                failScript(*error, "user");
            } else {
                set(State::Stopped, tr("live.stopped", language_));
            }
            return;
        }
    }
    auto interpreter =
        std::make_unique<Interpreter>(std::move(project), BlockCatalog::builtin(), board_, link, module.get());
    set(State::Running, tr("live.running", language_));
    const auto start = std::chrono::steady_clock::now();
    while (!stopping_) {
        {
            std::optional<project::Project> next;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                next.swap(pending_);
            }
            if (next) {
                interpreter->update(std::move(*next));
            }
        }
        interpreter->step(std::chrono::duration_cast<milliseconds>(std::chrono::steady_clock::now() - start));
        if (const auto error = linkError ? linkError : link.takeError()) {
            fail(*error);
            return;
        }
        if (module) {
            if (const auto error = module->takeError()) {
                link.reset();
                failScript(*error, "user");
                return;
            }
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.runningBlocks = interpreter->runningBlocks();
            snapshot_.variables = interpreter->variables();
            snapshot_.readings = interpreter->readings();
            for (std::string& line : interpreter->takeOutput()) {
                addOutput(std::move(line), false);
            }
        }
        if (interpreter->finished()) {
            // Finished, but still connected: an edit can start the blocks again.
            set(State::Finished, tr("live.finished", language_));
            while (!stopping_) {
                std::this_thread::sleep_for(milliseconds(20));
                const std::lock_guard<std::mutex> lock(mutex_);
                if (pending_) {
                    break;
                }
            }
            if (stopping_) {
                break;
            }
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                interpreter = std::make_unique<Interpreter>(std::move(*pending_), BlockCatalog::builtin(), board_, link,
                                                            module.get());
                pending_.reset();
            }
            set(State::Running, tr("live.running", language_));
            continue;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    link.reset();
    if (snapshot().state != State::Finished) {
        set(State::Stopped, tr("live.stopped", language_));
    }
}

script::Host Session::scriptHost(LiveLink& link, std::optional<Error>& linkError) {
    script::Host host;
    host.pins = &link;
    host.board = &board_;
    host.stop = &stopping_;
    host.print = [this](const std::string& line) {
        const std::lock_guard<std::mutex> lock(mutex_);
        addOutput(line, false);
    };
    host.printError = [this](const std::string& line) {
        const std::lock_guard<std::mutex> lock(mutex_);
        addOutput(line, true);
    };
    host.reading = [this](const std::string& label, double value) {
        const std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.readings[label] = value;
    };
    // Waits in short steps: Stop is felt at once, and an unplugged board stops the program.
    host.sleep = [this, &link, &linkError](milliseconds duration) {
        const auto end = std::chrono::steady_clock::now() + duration;
        do {
            if (auto error = link.takeError()) {
                linkError = std::move(error);
                stopping_ = true;
            }
            if (stopping_) {
                return false;
            }
            std::this_thread::sleep_for(std::min(milliseconds(10), duration));
        } while (std::chrono::steady_clock::now() < end);
        return true;
    };
    return host;
}

void Session::failScript(const script::ScriptError& error, const std::string& file) {
    const std::string message = (file.empty() ? std::string{} : tr("text.in_user_code", language_) + " ") +
                                script::describe(error, language_);
    set(State::Failed, message, error.details);
    const std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.errorLine = error.line;
    snapshot_.errorFile = file;
    // On the error output too, with what the interpreter said: it stays there to read after the stop.
    addOutput(message, true);
    if (!error.details.empty() && error.details != message) {
        std::istringstream details(error.details);
        std::string line;
        while (std::getline(details, line)) {
            if (!line.empty()) {
                addOutput(line, true);
            }
        }
    }
}

void Session::addOutput(std::string text, bool error) {
    snapshot_.output.push_back({std::move(text), error});
    ++snapshot_.outputCount;
    if (snapshot_.output.size() > kOutputLines) {
        snapshot_.output.erase(snapshot_.output.begin(),
                               snapshot_.output.end() - static_cast<std::ptrdiff_t>(kOutputLines));
    }
}

void Session::runScript(const project::Project& project, LiveLink& link) {
    std::optional<Error> linkError;
    script::Host host = scriptHost(link, linkError);
    set(State::Running, tr("text.running", language_));
    const auto error = script::run(project.language, project.code, host, language_);
    if (!linkError) {
        linkError = link.takeError();
    }
    link.reset();
    if (linkError) {
        set(State::Failed, tr(errorMessageKey(linkError->code), language_), linkError->detail);
        return;
    }
    if (error) {
        failScript(*error, "");
        return;
    }
    if (stopping_) {
        set(State::Stopped, tr("live.stopped", language_));
        return;
    }
    // Finished: the board stays connected until Stop (as for the blocks).
    set(State::Finished, tr("text.finished", language_));
}

Session::Opener Session::serialOpener() {
    return [](const std::string& port) -> Result<std::unique_ptr<serial::LineTransport>> {
        auto opened = serial::SerialPort::open(port, kBaud);
        if (!opened.ok()) {
            return opened.error();
        }
        return std::unique_ptr<serial::LineTransport>(std::move(opened).value());
    };
}

Session::Opener Session::opener() {
    return [serial = serialOpener()](const std::string& port) -> Result<std::unique_ptr<serial::LineTransport>> {
        if (const auto address = network::wifiAddress(port)) {
            auto opened = network::SocketConnection::openTcp(*address, network::kLivePort, milliseconds(5000));
            if (!opened.ok()) {
                return opened.error();
            }
            return std::unique_ptr<serial::LineTransport>(std::move(opened).value());
        }
        if (const auto address = network::bluetoothAddress(port)) {
            auto opened = network::SocketConnection::openBluetooth(*address, milliseconds(10000));
            if (!opened.ok()) {
                return opened.error();
            }
            return std::unique_ptr<serial::LineTransport>(std::move(opened).value());
        }
        return serial(port);
    };
}

Session::Flasher Session::arduinoCliFlasher(const Board& board, std::string port) {
    return [&board, port = std::move(port)](std::string& details) -> Status {
        const auto executable = toolchain::locateArduinoCli();
        if (executable.empty()) {
            return Error{ErrorCode::ProcessFailed, "arduino-cli not found"};
        }
        const toolchain::ArduinoCli cli(executable, toolchain::Directories::defaults());
        return uploadFirmware(cli, board, port, toolchain::userDataDirectory(), details);
    };
}

// --- firmware ----------------------------------------------------------------------------

std::string_view firmwareSource() {
    return resources::find("firmware/s2a_live/s2a_live.ino").value_or(std::string_view{});
}

Status uploadFirmware(const toolchain::ArduinoCli& cli, const Board& board, const std::string& port,
                      const std::filesystem::path& workDirectory, std::string& details) {
    const std::filesystem::path sketch = workDirectory / "sketches" / "s2a_live";
    std::error_code error;
    std::filesystem::create_directories(sketch, error);
    {
        std::ofstream file(sketch / "s2a_live.ino", std::ios::binary | std::ios::trunc);
        file << firmwareSource();
        if (!file) {
            return Error{ErrorCode::WriteFailed, sketch.u8string()};
        }
    }
    if (auto installed = cli.isPlatformInstalled(board.platform); installed.ok() && !installed.value()) {
        if (auto outcome = cli.installPlatform(board.platform); !outcome.ok()) {
            return outcome.error();
        }
    }
    if (auto missing = toolchain::missingLibraries(cli, toolchain::requiredLibraries(firmwareSource())); !missing.ok()) {
        return missing.error();
    } else if (!missing.value().empty()) {
        auto outcome = cli.installLibraries(missing.value());
        if (!outcome.ok()) {
            return outcome.error();
        }
        if (const auto diagnosis = toolchain::diagnoseInstall(outcome.value());
            diagnosis.problem != toolchain::Problem::None) {
            details = diagnosis.technical;
            return Error{ErrorCode::ProcessFailed, "the libraries of the live firmware cannot be installed"};
        }
    }
    const std::filesystem::path build = workDirectory / "build" / ("s2a_live-" + board.id);
    auto compiled = cli.compile(sketch, board, build);
    if (!compiled.ok()) {
        return compiled.error();
    }
    if (const auto diagnosis = toolchain::diagnoseCompile(compiled.value());
        diagnosis.problem != toolchain::Problem::None) {
        details = diagnosis.technical;
        return Error{ErrorCode::ProcessFailed, "the live firmware does not compile"};
    }
    auto uploaded = cli.upload(sketch, board, build, port);
    if (!uploaded.ok()) {
        return uploaded.error();
    }
    if (const auto diagnosis = toolchain::diagnoseUpload(uploaded.value(), port);
        diagnosis.problem != toolchain::Problem::None) {
        details = diagnosis.technical;
        return Error{ErrorCode::PortFailed, toolchain::describe(diagnosis, Language::English)};
    }
    return success();
}

} // namespace s2a::live
