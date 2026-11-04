#include "s2a/live.hpp"

#include "s2a/diagnostics.hpp"
#include "s2a/resources.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
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

} // namespace

// --- LiveLink ----------------------------------------------------------------------------

Status LiveLink::handshake(milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool asked = false;
    while (std::chrono::steady_clock::now() < deadline) {
        auto line = transport_.readLine(milliseconds(300));
        if (!line.ok()) {
            return line.error();
        }
        if (line.value() && *line.value() == kHello) {
            return success();
        }
        if (!line.value() && !asked) {
            // No greeting yet (the board was not reset by opening the port): ask for it.
            asked = true;
            if (auto sent = transport_.writeLine("?"); !sent.ok()) {
                return sent.error();
            }
        }
    }
    return Error{ErrorCode::BoardNotResponding, "no S2A live greeting"};
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

int Interpreter::pin(const Block& block, const std::string& name, PinMode mode) {
    const Pin* found = board_.findPin(field(block, name));
    if (found == nullptr) {
        return -1; // the validator reports it before Live starts
    }
    if (auto it = modes_.find(found->number); it == modes_.end() || it->second != mode) {
        pins_.setMode(found->number, mode);
        modes_[found->number] = mode;
    }
    return found->number;
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
        return number >= 0 && pins_.digitalRead(number) ? 1 : 0;
    }
    if (type == "pins_analog_read") {
        const int number = pin(block, "PIN", PinMode::Input);
        return number >= 0 ? pins_.analogRead(number) : 0;
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
        } else if (type == "pins_analog_write") {
            const double value = number(block, "VALUE");
            if (const int number = pin(block, "PIN", PinMode::Output); number >= 0) {
                pins_.analogWrite(number, static_cast<int>(std::clamp(value, 0.0, 255.0)));
            }
        } else if (type == "control_wait") {
            const double seconds = number(block, "SECONDS");
            thread.wakeAt = now + milliseconds(static_cast<long long>(std::max(0.0, seconds) * 1000));
            return;
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
