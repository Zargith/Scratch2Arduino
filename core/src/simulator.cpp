// Virtual board of the simulation: answers the Live mode's commands like firmware/s2a_live.
#include "s2a/simulator.hpp"

#include "s2a/live.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <thread>

namespace s2a::sim {

using std::chrono::milliseconds;

bool isSimulated(std::string_view port) noexcept {
    return port.substr(0, kPortPrefix.size()) == kPortPrefix;
}

namespace {

/// Text after the first `words` words of `command` (the text of the screen commands).
std::string textAfter(std::string_view command, int words) {
    std::size_t position = 0;
    for (int word = 0; word < words; ++word) {
        position = command.find(' ', position);
        if (position == std::string_view::npos) {
            return {};
        }
        ++position;
    }
    std::string text(command.substr(position));
    return text.size() > 16 ? text.substr(0, 16) : text;
}

} // namespace

VirtualBoard::VirtualBoard(const Board& board) : board_(board) {}

std::string VirtualBoard::label(int number) const {
    const Pin* pin = board_.findPin(number);
    return pin != nullptr ? pin->label : std::to_string(number);
}

std::optional<int> VirtualBoard::number(std::string_view label) const {
    const Pin* pin = board_.findPin(label);
    if (pin == nullptr) {
        return std::nullopt;
    }
    return pin->number;
}

void VirtualBoard::reset() {
    for (auto& [pin, output] : outputs_) {
        output = State::Output{};
    }
    servos_.clear();
    steppers_.clear();
    for (auto& [pin, shown] : digits_) {
        shown.clear();
    }
    tonePin_ = -1;
    toneFrequency_ = 0;
    toneEnd_.reset();
    lcd_ = {};
    lcd16_ = {};
    oled_ = {};
    for (auto& [pin, colours] : pixels_) {
        std::fill(colours.begin(), colours.end(), std::array<int, 3>{0, 0, 0});
    }
    for (auto& [pin, rows] : matrices_) {
        rows.fill(0);
    }
}

std::string VirtualBoard::answer(std::string_view command) {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::istringstream words{std::string(command)};
    std::string kind;
    words >> kind;
    const auto unknown = "! " + std::string(command);
    if (kind.size() != 1) {
        return unknown;
    }
    long pin = 0;
    long value = 0;
    switch (kind[0]) {
    case '?':
        return std::string(live::kHello);
    case 'X':
        reset();
        return "ok";
    case 'M': {
        std::string mode;
        if (!(words >> pin >> mode) || (mode != "O" && mode != "U" && mode != "I")) {
            return unknown;
        }
        modes_[static_cast<int>(pin)] = mode[0];
        if (mode == "O") {
            outputs_.try_emplace(static_cast<int>(pin));
        } else {
            outputs_.erase(static_cast<int>(pin));
        }
        return "ok";
    }
    case 'D':
        if (!(words >> pin >> value)) {
            return unknown;
        }
        servos_.erase(static_cast<int>(pin));
        outputs_[static_cast<int>(pin)] = State::Output{value != 0 ? 255 : 0, false};
        modes_[static_cast<int>(pin)] = 'O';
        return "ok";
    case 'P':
        if (!(words >> pin >> value)) {
            return unknown;
        }
        servos_.erase(static_cast<int>(pin));
        outputs_[static_cast<int>(pin)] = State::Output{static_cast<int>(std::clamp(value, 0L, 255L)), true};
        modes_[static_cast<int>(pin)] = 'O';
        return "ok";
    case 'R': {
        if (!(words >> pin)) {
            return unknown;
        }
        const int number = static_cast<int>(pin);
        bool high = false;
        if (const auto output = outputs_.find(number); output != outputs_.end()) {
            high = output->second.level > 0;
        } else {
            const auto mode = modes_.find(number);
            const bool pullup = mode != modes_.end() && mode->second == 'U';
            const auto button = pressed_.find(number);
            const bool pressed = button != pressed_.end() && button->second;
            high = pullup ? !pressed : pressed;
        }
        return "r " + std::to_string(number) + (high ? " 1" : " 0");
    }
    case 'A': {
        if (!(words >> pin)) {
            return unknown;
        }
        const auto found = analog_.find(static_cast<int>(pin));
        return "a " + std::to_string(pin) + " " + std::to_string(found != analog_.end() ? found->second : kDefaultAnalog);
    }
    case 'T': {
        long duration = 0;
        if (!(words >> pin >> value >> duration)) {
            return unknown;
        }
        if (value <= 0) {
            if (tonePin_ == pin) {
                tonePin_ = -1;
                toneFrequency_ = 0;
                toneEnd_.reset();
            }
        } else {
            servos_.erase(static_cast<int>(pin));
            tonePin_ = static_cast<int>(pin);
            toneFrequency_ = static_cast<int>(std::clamp(value, 31L, 65535L));
            toneEnd_ = duration > 0 ? std::optional(Clock::now() + milliseconds(duration)) : std::nullopt;
        }
        return "ok";
    }
    case 'F': {
        long dio = 0;
        if (!(words >> pin >> dio)) {
            return unknown;
        }
        std::string shown;
        if (words >> value) { // the firmware's rule: right-aligned, "----" when it does not fit
            shown = value > 9999 || value < -999 ? "----" : std::to_string(value);
            shown.insert(0, 4 - std::min<std::size_t>(shown.size(), 4), ' ');
        }
        digits_[static_cast<int>(pin)] = shown;
        return "ok";
    }
    case 'Q': {
        // Stepper motor: the move is done at once (the board answers when it is over).
        std::array<long, 4> pins{};
        long steps = 0;
        if (!(words >> pins[0] >> pins[1] >> pins[2] >> pins[3] >> steps >> value)) {
            return unknown;
        }
        for (const long coil : pins) {
            servos_.erase(static_cast<int>(coil));
            modes_[static_cast<int>(coil)] = 'O';
            outputs_[static_cast<int>(coil)] = State::Output{}; // coils off after the move
        }
        steppers_[static_cast<int>(pins[0])] += steps;
        return "ok";
    }
    case 'S':
        if (!(words >> pin >> value)) {
            return unknown;
        }
        outputs_.erase(static_cast<int>(pin));
        servos_[static_cast<int>(pin)] = static_cast<int>(std::clamp(value, 0L, 180L));
        return "ok";
    case 'U': {
        long echo = 0;
        if (!(words >> pin >> echo)) {
            return unknown;
        }
        const auto found = distances_.find(static_cast<int>(pin));
        const double centimetres = found != distances_.end() ? found->second : kDefaultDistance;
        return "u " + std::to_string(pin) + " " + std::to_string(std::lround(centimetres * 10));
    }
    case 'H': {
        long model = 0;
        if (!(words >> pin >> model)) {
            return unknown;
        }
        const auto found = climates_.find(static_cast<int>(pin));
        const auto [temperature, humidity] =
            found != climates_.end() ? found->second : std::pair{kDefaultTemperature, kDefaultHumidity};
        return "h " + std::to_string(pin) + " " + std::to_string(std::lround(temperature * 10)) + " " +
               std::to_string(std::lround(humidity * 10));
    }
    case 'L':
    case 'O': {
        long row = 0;
        if (!(words >> row)) {
            return unknown;
        }
        const std::string text = textAfter(command, 2);
        if (kind[0] == 'L') {
            if (row == 0) {
                lcd_ = {};
            } else if (row <= 2) {
                lcd_.at(static_cast<std::size_t>(row - 1)) = text;
            }
        } else if (row == 0) {
            oled_ = {};
        } else if (row <= 8) {
            oled_.at(static_cast<std::size_t>(row - 1)) = text;
        }
        return "ok";
    }
    case 'C': {
        std::array<long, 6> pins{};
        long row = 0;
        for (long& lcdPin : pins) {
            words >> lcdPin;
        }
        if (!(words >> row)) {
            return unknown;
        }
        if (row == 0) {
            lcd16_ = {};
        } else if (row <= 2) {
            lcd16_.at(static_cast<std::size_t>(row - 1)) = textAfter(command, 8);
        }
        return "ok";
    }
    case 'N': {
        long count = 0;
        long index = 0;
        std::array<long, 3> colour{};
        if (!(words >> pin >> count >> index >> colour[0] >> colour[1] >> colour[2])) {
            return unknown;
        }
        auto& strip = pixels_[static_cast<int>(pin)];
        strip.resize(static_cast<std::size_t>(std::clamp(count, 1L, 100L)), {0, 0, 0});
        const std::array<int, 3> rgb{static_cast<int>(std::clamp(colour[0], 0L, 255L)),
                                     static_cast<int>(std::clamp(colour[1], 0L, 255L)),
                                     static_cast<int>(std::clamp(colour[2], 0L, 255L))};
        if (index == 0) {
            std::fill(strip.begin(), strip.end(), rgb);
        } else if (index <= static_cast<long>(strip.size())) {
            strip.at(static_cast<std::size_t>(index - 1)) = rgb;
        }
        return "ok";
    }
    case 'G': {
        long clk = 0;
        long cs = 0;
        long row = 0;
        if (!(words >> pin >> clk >> cs >> row >> value) || row < 1 || row > 8) {
            return unknown;
        }
        matrices_[static_cast<int>(pin)].at(static_cast<std::size_t>(row - 1)) = static_cast<int>(value & 0xFF);
        return "ok";
    }
    case 'B': {
        std::string message = std::move(phoneMessage_);
        phoneMessage_.clear();
        return message.empty() ? "b" : "b " + message;
    }
    case 'Y':
        phone_.push_back(command.size() > 2 ? std::string(command.substr(2)) : std::string());
        while (phone_.size() > kPhoneLines) {
            phone_.pop_front();
        }
        return "ok";
    case 'J':
    case 'W':
        return "ok"; // the phone talks to the virtual board directly: no module, no network
    case 'K':
        return "k -";
    default:
        return unknown;
    }
}

VirtualBoard::State VirtualBoard::state() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    State state;
    for (const auto& [pin, mode] : modes_) {
        state.modes[label(pin)] = mode == 'O' ? "output" : (mode == 'U' ? "pullup" : "input");
    }
    for (const auto& [pin, output] : outputs_) {
        state.outputs[label(pin)] = output;
    }
    for (const auto& [pin, angle] : servos_) {
        state.servos[label(pin)] = angle;
    }
    for (const auto& [pin, shown] : digits_) {
        state.digits[label(pin)] = shown;
    }
    for (const auto& [pin, steps] : steppers_) {
        state.steppers[label(pin)] = static_cast<double>(steps) * 360.0 / static_cast<double>(live::kStepsPerTurn);
    }
    if (tonePin_ >= 0 && (!toneEnd_ || Clock::now() < *toneEnd_)) {
        state.tones[label(tonePin_)] = toneFrequency_;
    }
    state.lcd = lcd_;
    state.lcd16 = lcd16_;
    state.oled = oled_;
    for (const auto& [pin, colours] : pixels_) {
        state.pixels[label(pin)] = colours;
    }
    for (const auto& [pin, rows] : matrices_) {
        state.matrices[label(pin)] = rows;
    }
    state.phone.assign(phone_.begin(), phone_.end());
    for (const auto& [pin, pressed] : pressed_) {
        state.pressed[label(pin)] = pressed;
    }
    for (const auto& [pin, value] : analog_) {
        state.analog[label(pin)] = value;
    }
    for (const auto& [pin, centimetres] : distances_) {
        state.distances[label(pin)] = centimetres;
    }
    for (const auto& [pin, climate] : climates_) {
        state.climates[label(pin)] = climate;
    }
    return state;
}

void VirtualBoard::press(std::string_view pin, bool pressed) {
    if (const auto found = number(pin)) {
        const std::lock_guard<std::mutex> lock(mutex_);
        pressed_[*found] = pressed;
    }
}

void VirtualBoard::setAnalog(std::string_view pin, int value) {
    if (const auto found = number(pin)) {
        const std::lock_guard<std::mutex> lock(mutex_);
        analog_[*found] = std::clamp(value, 0, 1023);
    }
}

void VirtualBoard::setDistance(std::string_view trigPin, double centimetres) {
    if (const auto found = number(trigPin)) {
        const std::lock_guard<std::mutex> lock(mutex_);
        distances_[*found] = std::clamp(centimetres, 0.0, 400.0);
    }
}

void VirtualBoard::setClimate(std::string_view pin, double temperature, double humidity) {
    if (const auto found = number(pin)) {
        const std::lock_guard<std::mutex> lock(mutex_);
        climates_[*found] = {std::clamp(temperature, -40.0, 80.0), std::clamp(humidity, 0.0, 100.0)};
    }
}

void VirtualBoard::phoneSays(std::string text) {
    std::replace_if(text.begin(), text.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    const std::lock_guard<std::mutex> lock(mutex_);
    phoneMessage_ = std::move(text);
}

// --- Connection -----------------------------------------------------------------------------

Connection::Connection(std::shared_ptr<VirtualBoard> board) : board_(std::move(board)) {
    pending_.emplace_back(live::kHello);
}

Status Connection::writeLine(std::string_view line) {
    pending_.push_back(board_->answer(line));
    return success();
}

Result<std::optional<std::string>> Connection::readLine(milliseconds timeout) {
    if (pending_.empty()) {
        // Nothing to say, like a quiet board: wait a little so that a polling loop does not spin.
        std::this_thread::sleep_for(std::min(timeout, milliseconds(10)));
        return std::optional<std::string>();
    }
    std::string line = std::move(pending_.front());
    pending_.pop_front();
    return std::optional<std::string>(std::move(line));
}

} // namespace s2a::sim
