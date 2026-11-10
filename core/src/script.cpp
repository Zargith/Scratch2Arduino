#include "s2a/script.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace s2a::script {

namespace detail {

namespace {

using std::chrono::milliseconds;

/// Waits in short steps, so that Stop is felt at once even during a long delay().
bool defaultSleep(milliseconds duration, const std::atomic<bool>* stop) {
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        if (stop != nullptr && stop->load()) {
            return false;
        }
        std::this_thread::sleep_for(std::min<milliseconds>(
            milliseconds(10),
            std::chrono::duration_cast<milliseconds>(end - std::chrono::steady_clock::now()) + milliseconds(1)));
    }
    return stop == nullptr || !stop->load();
}

int clampInt(double value, int low, int high) {
    if (std::isnan(value)) {
        return low;
    }
    return static_cast<int>(std::lround(std::clamp(value, static_cast<double>(low), static_cast<double>(high))));
}

} // namespace

Api::Api(Host& host, Language language) : host_(host), language_(language) {}

int Api::pin(const std::string& label) const {
    const Pin* found = host_.board != nullptr ? host_.board->findPin(label) : nullptr;
    if (found == nullptr) {
        throw ApiError{tr("text.pin_unknown", language_,
                          {{"pin", label}, {"board", host_.board != nullptr ? host_.board->name : "?"}})};
    }
    return found->number;
}

int Api::pin(long long number) const {
    const Pin* found = host_.board != nullptr && number >= 0 && number < 1000
                           ? host_.board->findPin(static_cast<int>(number))
                           : nullptr;
    if (found == nullptr) {
        throw ApiError{tr("text.pin_unknown", language_,
                          {{"pin", std::to_string(number)},
                           {"board", host_.board != nullptr ? host_.board->name : "?"}})};
    }
    return found->number;
}

std::string Api::label(int pin) const {
    const Pin* found = host_.board != nullptr ? host_.board->findPin(pin) : nullptr;
    return found != nullptr ? found->label : std::to_string(pin);
}

bool Api::stopRequested() const {
    return host_.stop != nullptr && host_.stop->load();
}

void Api::checkStop() const {
    if (stopRequested()) {
        throw Stopped{};
    }
}

void Api::ensureMode(int pin, live::PinMode mode) {
    auto it = modes_.find(pin);
    // Reading a pin set as an input with pull-up keeps the pull-up (as the blocks do).
    const bool keep = it != modes_.end() && mode == live::PinMode::Input && it->second == live::PinMode::InputPullup;
    if (!keep && (it == modes_.end() || it->second != mode)) {
        host_.pins->setMode(pin, mode);
        modes_[pin] = mode;
    }
}

void Api::pinMode(int pin, int mode) {
    checkStop();
    switch (mode) {
    case kInput:
        host_.pins->setMode(pin, live::PinMode::Input);
        modes_[pin] = live::PinMode::Input;
        return;
    case kOutput:
        host_.pins->setMode(pin, live::PinMode::Output);
        modes_[pin] = live::PinMode::Output;
        return;
    case kInputPullup:
        host_.pins->setMode(pin, live::PinMode::InputPullup);
        modes_[pin] = live::PinMode::InputPullup;
        return;
    default:
        throw ApiError{tr("text.pin_mode_unknown", language_)};
    }
}

void Api::digitalWrite(int pin, bool high) {
    checkStop();
    ensureMode(pin, live::PinMode::Output);
    host_.pins->digitalWrite(pin, high);
}

int Api::digitalRead(int pin) {
    checkStop();
    ensureMode(pin, live::PinMode::Input);
    const int value = host_.pins->digitalRead(pin) ? 1 : 0;
    if (host_.reading) {
        host_.reading(label(pin), value);
    }
    return value;
}

int Api::analogRead(int pin) {
    checkStop();
    const Pin* found = host_.board != nullptr ? host_.board->findPin(pin) : nullptr;
    if (found == nullptr || !found->has(PinCapability::Analog)) {
        throw ApiError{tr("text.pin_not_analog", language_, {{"pin", label(pin)}})};
    }
    const int value = host_.pins->analogRead(pin);
    if (host_.reading) {
        host_.reading(label(pin), value);
    }
    return value;
}

void Api::analogWrite(int pin, double value) {
    checkStop();
    const Pin* found = host_.board != nullptr ? host_.board->findPin(pin) : nullptr;
    if (found == nullptr || !found->has(PinCapability::Pwm)) {
        throw ApiError{tr("text.pin_not_pwm", language_, {{"pin", label(pin)}})};
    }
    ensureMode(pin, live::PinMode::Output);
    host_.pins->analogWrite(pin, clampInt(value, 0, 255));
}

void Api::delay(double ms) {
    checkStop();
    const milliseconds duration(clampInt(ms, 0, 24 * 3600 * 1000));
    const bool goOn = host_.sleep ? host_.sleep(duration) : defaultSleep(duration, host_.stop);
    if (!goOn) {
        throw Stopped{};
    }
}

double Api::millis() const {
    if (host_.now) {
        return static_cast<double>(host_.now().count());
    }
    return static_cast<double>(
        std::chrono::duration_cast<milliseconds>(std::chrono::steady_clock::now() - start_).count());
}

void Api::tone(int pin, double frequency, double ms) {
    checkStop();
    ensureMode(pin, live::PinMode::Output);
    host_.pins->tone(pin, clampInt(frequency, 0, 20000), clampInt(ms, 0, 3600 * 1000));
}

void Api::noTone(int pin) {
    checkStop();
    host_.pins->tone(pin, 0, 0);
}

void Api::servoWrite(int pin, double angle) {
    checkStop();
    host_.pins->servoWrite(pin, clampInt(angle, 0, 180));
}

void Api::print(const std::string& text) {
    if (host_.print) {
        host_.print(text);
    }
}

std::vector<std::pair<std::string, int>> Api::constants() const {
    std::vector<std::pair<std::string, int>> result{
        {"HIGH", 1}, {"LOW", 0}, {"INPUT", kInput}, {"OUTPUT", kOutput}, {"INPUT_PULLUP", kInputPullup}};
    if (host_.board != nullptr) {
        if (host_.board->builtinLed >= 0) {
            result.emplace_back("LED_BUILTIN", host_.board->builtinLed);
        }
        for (const Pin& pin : host_.board->pins) {
            if (pin.label.size() >= 2 && pin.label[0] == 'A' &&
                std::all_of(pin.label.begin() + 1, pin.label.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                result.emplace_back(pin.label, pin.number);
            }
        }
    }
    return result;
}

} // namespace detail

bool isScript(project::ProgramLanguage language) noexcept {
    return language == project::ProgramLanguage::Python || language == project::ProgramLanguage::JavaScript;
}

std::optional<ScriptError> check(project::ProgramLanguage language, const std::string& code, Language userLanguage) {
    if (language == project::ProgramLanguage::Python) {
        return detail::checkPython(code, userLanguage);
    }
    if (language == project::ProgramLanguage::JavaScript) {
        return detail::checkJavaScript(code, userLanguage);
    }
    return ScriptError{0, tr("text.not_a_script", userLanguage), {}};
}

std::optional<ScriptError> run(project::ProgramLanguage language, const std::string& code, Host& host,
                               Language userLanguage) {
    if (host.pins == nullptr || host.board == nullptr || !isScript(language)) {
        return ScriptError{0, tr("text.not_a_script", userLanguage), {}};
    }
    detail::Api api(host, userLanguage);
    if (language == project::ProgramLanguage::Python) {
        return detail::runPython(code, api, userLanguage);
    }
    return detail::runJavaScript(code, api, userLanguage);
}

std::string describe(const ScriptError& error, Language userLanguage) {
    if (error.line <= 0) {
        return error.message;
    }
    return tr("text.at_line", userLanguage, {{"line", std::to_string(error.line)}, {"message", error.message}});
}

} // namespace s2a::script

namespace s2a::script {

std::unique_ptr<Module> Module::load(project::ProgramLanguage language, const std::string& code, Host& host,
                                     Language userLanguage, std::optional<ScriptError>& error) {
    error.reset();
    if (host.pins == nullptr || host.board == nullptr || !isScript(language)) {
        error = ScriptError{0, tr("text.not_a_script", userLanguage), {}};
        return nullptr;
    }
    auto api = std::make_unique<detail::Api>(host, userLanguage);
    if (language == project::ProgramLanguage::Python) {
        return detail::loadPython(code, std::move(api), userLanguage, error);
    }
    return detail::loadJavaScript(code, std::move(api), userLanguage, error);
}

} // namespace s2a::script
