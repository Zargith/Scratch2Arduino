#include "s2a/live.hpp"

#include "s2a/codegen.hpp"
#include "s2a/diagnostics.hpp"
#include "s2a/resources.hpp"
#include "s2a/validator.hpp"

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

using project::Block;
using project::Stack;
using std::chrono::milliseconds;

constexpr int kStepBudget = 500; ///< Blocks run per script and step before giving way.

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

std::optional<std::string> LiveLink::request(const std::string& command) {
    if (error_) {
        return std::nullopt;
    }
    if (auto sent = transport_.writeLine(command); !sent.ok()) {
        error_ = sent.error();
        return std::nullopt;
    }
    for (;;) {
        auto line = transport_.readLine(milliseconds(500));
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

void LiveLink::reset() {
    (void)request("X");
}

std::optional<Error> LiveLink::takeError() {
    auto error = std::move(error_);
    error_.reset();
    return error;
}

// --- Interpreter -------------------------------------------------------------------------

Interpreter::Interpreter(project::Project project, const BlockCatalog& catalog, const Board& board, Pins& pins)
    : project_(std::move(project)), catalog_(catalog), board_(board), pins_(pins) {
    for (const project::Variable& variable : project_.variables) {
        variables_[variable.id] = variable.initialValue;
    }
    start();
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
                pins_.digitalWrite(board_.builtinLed, field(block, "STATE") != "OFF");
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
                modes_[number] = PinMode::Output; // tone() sets the pin
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
            if (parent != nullptr) {
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
    // The blocks are checked first, as for an upload.
    const auto issues = validation::validate(project, BlockCatalog::builtin(), &board_, language_);
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
    auto interpreter = std::make_unique<Interpreter>(std::move(project), BlockCatalog::builtin(), board_, link);
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
        if (const auto error = link.takeError()) {
            fail(*error);
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.runningBlocks = interpreter->runningBlocks();
            snapshot_.variables = interpreter->variables();
            snapshot_.readings = interpreter->readings();
            for (std::string& line : interpreter->takeOutput()) {
                snapshot_.output.push_back(std::move(line));
                ++snapshot_.outputCount;
            }
            if (snapshot_.output.size() > kOutputLines) {
                snapshot_.output.erase(snapshot_.output.begin(),
                                       snapshot_.output.end() - static_cast<std::ptrdiff_t>(kOutputLines));
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
                interpreter = std::make_unique<Interpreter>(std::move(*pending_), BlockCatalog::builtin(), board_, link);
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

Session::Opener Session::serialOpener() {
    return [](const std::string& port) -> Result<std::unique_ptr<serial::LineTransport>> {
        auto opened = serial::SerialPort::open(port, kBaud);
        if (!opened.ok()) {
            return opened.error();
        }
        return std::unique_ptr<serial::LineTransport>(std::move(opened).value());
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
