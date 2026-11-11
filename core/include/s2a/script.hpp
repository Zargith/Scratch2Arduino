#pragma once

#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/live.hpp"
#include "s2a/project.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/// Python and JavaScript modes: a program typed as text runs on the computer and drives the
/// board through the Live mode (like the blocks in Live mode), with a small Arduino-like API:
///
///   pinMode(pin, mode)            mode: OUTPUT, INPUT, INPUT_PULLUP
///   digitalWrite(pin, value)      value: HIGH / LOW (or true / false, 1 / 0)
///   digitalRead(pin)              -> HIGH or LOW
///   analogRead(pin)               -> 0-1023 (pins A0, A1...)
///   analogWrite(pin, value)       0-255, on the PWM pins (~)
///   delay(milliseconds), millis()
///   tone(pin, frequency[, duration]), noTone(pin)
///   servoWrite(pin, angle)        0-180
///   print(...)                    to the Live panel and the serial monitor (console.log in JavaScript)
///
/// Pins are numbers (13) or labels ("A0", "D5"); A0..A7, LED_BUILTIN, HIGH, LOW, INPUT, OUTPUT,
/// INPUT_PULLUP are defined. A program may also define setup() and loop() as on an Arduino:
/// setup() runs once after the program, then loop() again and again. The same names are
/// available in an `arduino` module (Python: import arduino) / object (JavaScript).
namespace s2a::script {

/// A mistake found while checking or running a program, at a line of the code.
struct ScriptError {
    int line = 0;        ///< 1-based; 0 when unknown.
    std::string message; ///< In the user's language when it could be explained.
    std::string details; ///< What the interpreter said, for "show details" and bug reports.
};

/// What a running program can use. Every function is called from the program's thread.
struct Host {
    live::Pins* pins = nullptr;
    const Board* board = nullptr;
    /// A line written by print() (the standard output).
    std::function<void(const std::string&)> print;
    /// A line written by printError() / console.error() (the error output); print() when not set.
    std::function<void(const std::string&)> printError;
    /// A value read on a pin (label "A0", "2"), for the Live panel and its graphs.
    std::function<void(const std::string&, double)> reading;
    /// Waits; returns false when the program must stop (Stop button). Defaults to a real wait.
    std::function<bool(std::chrono::milliseconds)> sleep;
    /// Time since the program started. Defaults to the real time.
    std::function<std::chrono::milliseconds()> now;
    /// Set by another thread to stop the program, even in an endless loop.
    const std::atomic<bool>* stop = nullptr;
};

/// True for the languages run by this module (Python and JavaScript).
[[nodiscard]] bool isScript(project::ProgramLanguage language) noexcept;

/// Checks that the program can be read (syntax), without running it.
[[nodiscard]] std::optional<ScriptError> check(project::ProgramLanguage language, const std::string& code,
                                               Language userLanguage);

/// Runs the program until it ends, fails or is stopped. nullopt when it ended or was stopped.
/// Only one Python program runs at a time in the process: a second one waits for the first.
[[nodiscard]] std::optional<ScriptError> run(project::ProgramLanguage language, const std::string& code,
                                             Host& host, Language userLanguage);

/// "Line 3: ..." (or the message alone when the line is unknown), in `userLanguage`.
[[nodiscard]] std::string describe(const ScriptError& error, Language userLanguage);

class Module;

namespace detail {

/// A mistake in a call to the Arduino API ("pin 99 does not exist"), turned into a Python or
/// JavaScript error by the engines.
struct ApiError {
    std::string message;
};

/// Thrown by the API when the program must stop.
struct Stopped {};

/// The Arduino API shared by the Python and JavaScript engines (pin modes set automatically
/// when needed, as the blocks do).
class Api {
public:
    Api(Host& host, Language language);

    /// Pin number of a label ("A0", "13") or number; throws ApiError when the board has no such pin.
    [[nodiscard]] int pin(const std::string& label) const;
    [[nodiscard]] int pin(long long number) const;

    void pinMode(int pin, int mode);
    void digitalWrite(int pin, bool high);
    [[nodiscard]] int digitalRead(int pin);
    [[nodiscard]] int analogRead(int pin);
    void analogWrite(int pin, double value);
    void delay(double milliseconds);
    [[nodiscard]] double millis() const;
    void tone(int pin, double frequency, double milliseconds);
    void noTone(int pin);
    void servoWrite(int pin, double angle);
    void print(const std::string& text);
    void printError(const std::string& text);
    /// Throws Stopped when the Stop button was pressed.
    void checkStop() const;
    [[nodiscard]] bool stopRequested() const;
    /// The flag set by the Stop button (may be null).
    [[nodiscard]] const std::atomic<bool>* stopFlag() const { return host_.stop; }

    /// The constants of the API: HIGH, LOW, INPUT, OUTPUT, INPUT_PULLUP, LED_BUILTIN, A0...
    [[nodiscard]] std::vector<std::pair<std::string, int>> constants() const;

    static constexpr int kInput = 0;
    static constexpr int kOutput = 1;
    static constexpr int kInputPullup = 2;

private:
    void ensureMode(int pin, live::PinMode mode);
    [[nodiscard]] std::string label(int pin) const;

    Host& host_;
    Language language_;
    std::map<int, live::PinMode> modes_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

std::unique_ptr<Module> loadPython(const std::string& code, std::unique_ptr<Api> api, Language language,
                                   std::optional<ScriptError>& error);
std::unique_ptr<Module> loadJavaScript(const std::string& code, std::unique_ptr<Api> api, Language language,
                                       std::optional<ScriptError>& error);
std::optional<ScriptError> checkPython(const std::string& code, Language language);
std::optional<ScriptError> runPython(const std::string& code, Api& api, Language language);
std::optional<ScriptError> checkJavaScript(const std::string& code, Language language);
std::optional<ScriptError> runJavaScript(const std::string& code, Api& api, Language language);

} // namespace detail

/// The "My code" file of a block project, loaded once for the Live mode: its top-level
/// statements run at load, then the blocks call its functions and use its variables. The
/// module keeps the interpreter (one Python module at a time in the process).
class Module : public live::UserCode {
public:
    /// Loads `code`; null, with `error` set, when its top-level statements fail.
    [[nodiscard]] static std::unique_ptr<Module> load(project::ProgramLanguage language, const std::string& code,
                                                      Host& host, Language userLanguage,
                                                      std::optional<ScriptError>& error);

    [[nodiscard]] bool failed() const override { return error_.has_value() || failedOnce_; }
    /// The error that made the code fail (line in "My code"), once.
    [[nodiscard]] std::optional<ScriptError> takeError() {
        auto error = error_;
        error_.reset();
        failedOnce_ = failedOnce_ || error.has_value();
        return error;
    }

protected:
    explicit Module(std::unique_ptr<detail::Api> api) : api_(std::move(api)) {}
    /// Records the first error (none once Stop was pressed: stopping is not a mistake).
    void fail(ScriptError error) {
        if (!error_ && !failedOnce_ && !api_->stopRequested()) {
            error_ = std::move(error);
        }
    }

    std::unique_ptr<detail::Api> api_;
    std::optional<ScriptError> error_;
    bool failedOnce_ = false;
};

} // namespace s2a::script
