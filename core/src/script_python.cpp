// Python mode: runs the program with pocketpy (https://github.com/pocketpy/pocketpy).
#include "s2a/script.hpp"

#include <pocketpy.h>

#include <cmath>
#include <ctime>
#include <limits>
#include <mutex>
#include <regex>

namespace {

/// Stop flag of the running program, read by s2a_pocketpy_clock() (another thread sets it).
std::atomic<const std::atomic<bool>*> stopFlag{nullptr};

} // namespace

/// pocketpy's clock (see core/third_party/s2a_pocketpy.c): the real one, or the end of time when
/// the program must stop, which trips the watchdog at the next step of the interpreter.
extern "C" std::clock_t s2a_pocketpy_clock(void) {
    const std::atomic<bool>* stop = stopFlag.load();
    if (stop != nullptr && stop->load()) {
        return std::numeric_limits<std::clock_t>::max();
    }
    return std::clock();
}

namespace s2a::script::detail {

namespace {

constexpr const char* kFileName = "main.py";
/// Watchdog deadline, renewed at every call to the API: only Stop is meant to trip it, but a
/// program computing for an hour without touching the board ends too.
constexpr py_i64 kWatchdogMs = 3600 * 1000;

/// pocketpy has one interpreter per process: one Python program at a time.
std::mutex& pythonMutex() {
    static std::mutex mutex;
    return mutex;
}

/// The running program's API and output (valid while pythonMutex() is held by run).
struct Running {
    Api* api = nullptr;
    std::string pending; ///< print() output not ended by a newline yet.
    bool stopped = false;
    Language language = Language::French;
};
Running* running = nullptr;

void initialize() {
    static std::once_flag once;
    std::call_once(once, [] { py_initialize(); });
}

/// Calls `body` and turns the API's C++ exceptions into Python ones.
template <typename Body> bool guarded(Body body) {
    try {
        py_watchdog_begin(kWatchdogMs);
        return body();
    } catch (const ApiError& error) {
        return py_exception(tp_ValueError, "%s", error.message.c_str());
    } catch (const Stopped&) {
        running->stopped = true;
        return py_exception(tp_KeyboardInterrupt, "stopped");
    }
}

/// A pin argument: a number (13), a label ("A0") or a constant (A0, LED_BUILTIN).
int pinArg(py_Ref arg) {
    if (py_istype(arg, tp_str)) {
        return running->api->pin(std::string(py_tostr(arg)));
    }
    if (py_istype(arg, tp_int)) {
        return running->api->pin(static_cast<long long>(py_toint(arg)));
    }
    if (py_istype(arg, tp_float)) {
        return running->api->pin(static_cast<long long>(py_tofloat(arg)));
    }
    throw ApiError{tr("text.pin_expected", running->language)};
}

double numberArg(py_Ref arg) {
    if (py_istype(arg, tp_bool)) {
        return py_tobool(arg) ? 1 : 0;
    }
    if (py_istype(arg, tp_int)) {
        return static_cast<double>(py_toint(arg));
    }
    if (py_istype(arg, tp_float)) {
        return py_tofloat(arg);
    }
    throw ApiError{tr("text.number_expected", running->language)};
}

bool argc(int count, int low, int high) {
    if (count < low || count > high) {
        throw ApiError{tr("text.argument_count", running->language)};
    }
    return true;
}

bool pinModeFn(int count, py_StackRef argv) {
    return guarded([&] {
        argc(count, 2, 2);
        running->api->pinMode(pinArg(py_arg(0)), static_cast<int>(numberArg(py_arg(1))));
        py_newnone(py_retval());
        return true;
    });
}

bool digitalWriteFn(int count, py_StackRef argv) {
    return guarded([&] {
        argc(count, 2, 2);
        running->api->digitalWrite(pinArg(py_arg(0)), numberArg(py_arg(1)) != 0);
        py_newnone(py_retval());
        return true;
    });
}

bool digitalReadFn(int count, py_StackRef argv) {
    return guarded([&] {
        argc(count, 1, 1);
        py_newint(py_retval(), running->api->digitalRead(pinArg(py_arg(0))));
        return true;
    });
}

bool analogReadFn(int count, py_StackRef argv) {
    return guarded([&] {
        argc(count, 1, 1);
        py_newint(py_retval(), running->api->analogRead(pinArg(py_arg(0))));
        return true;
    });
}

bool analogWriteFn(int count, py_StackRef argv) {
    return guarded([&] {
        argc(count, 2, 2);
        running->api->analogWrite(pinArg(py_arg(0)), numberArg(py_arg(1)));
        py_newnone(py_retval());
        return true;
    });
}

bool delayFn(int count, py_StackRef argv) {
    return guarded([&] {
        argc(count, 1, 1);
        running->api->delay(numberArg(py_arg(0)));
        py_newnone(py_retval());
        return true;
    });
}

bool millisFn(int count, py_StackRef /*argv*/) {
    return guarded([&] {
        argc(count, 0, 0);
        py_newint(py_retval(), static_cast<py_i64>(running->api->millis()));
        return true;
    });
}

bool toneFn(int count, py_StackRef argv) {
    return guarded([&] {
        argc(count, 2, 3);
        running->api->tone(pinArg(py_arg(0)), numberArg(py_arg(1)), count > 2 ? numberArg(py_arg(2)) : 0);
        py_newnone(py_retval());
        return true;
    });
}

bool noToneFn(int count, py_StackRef argv) {
    return guarded([&] {
        argc(count, 1, 1);
        running->api->noTone(pinArg(py_arg(0)));
        py_newnone(py_retval());
        return true;
    });
}

bool servoWriteFn(int count, py_StackRef argv) {
    return guarded([&] {
        argc(count, 2, 2);
        running->api->servoWrite(pinArg(py_arg(0)), numberArg(py_arg(1)));
        py_newnone(py_retval());
        return true;
    });
}

/// print() of pocketpy writes pieces of text: complete lines go to the Live panel.
void printCallback(const char* text) {
    if (running == nullptr) {
        return;
    }
    running->pending += text;
    std::size_t newline = 0;
    while ((newline = running->pending.find('\n')) != std::string::npos) {
        running->api->print(running->pending.substr(0, newline));
        running->pending.erase(0, newline + 1);
    }
}

int noInput() {
    return -1; // input(): no keyboard for a program driving a board
}

void bindApi(py_Ref module, const Api& api) {
    py_bindfunc(module, "pinMode", pinModeFn);
    py_bindfunc(module, "digitalWrite", digitalWriteFn);
    py_bindfunc(module, "digitalRead", digitalReadFn);
    py_bindfunc(module, "analogRead", analogReadFn);
    py_bindfunc(module, "analogWrite", analogWriteFn);
    py_bindfunc(module, "delay", delayFn);
    py_bindfunc(module, "millis", millisFn);
    py_bindfunc(module, "tone", toneFn);
    py_bindfunc(module, "noTone", noToneFn);
    py_bindfunc(module, "servoWrite", servoWriteFn);
    for (const auto& [name, value] : api.constants()) {
        py_Ref slot = py_pushtmp();
        py_newint(slot, value);
        py_setdict(module, py_name(name.c_str()), slot);
        py_pop();
    }
}

/// The last 'line N' of the traceback that is in the user's program.
int lineOf(const std::string& traceback) {
    static const std::regex pattern(R"(File "main\.py", line (\d+))");
    int line = 0;
    for (auto it = std::sregex_iterator(traceback.begin(), traceback.end(), pattern); it != std::sregex_iterator();
         ++it) {
        line = std::stoi((*it)[1].str());
    }
    return line;
}

/// The current exception, explained, then cleared.
ScriptError takeError(Language language) {
    ScriptError error;
    char* formatted = py_formatexc();
    if (formatted != nullptr) {
        error.details = formatted;
        py_free(formatted);
    }
    error.line = lineOf(error.details);
    // Last line: "NameError: name 'x' is not defined".
    std::string last = error.details;
    while (!last.empty() && (last.back() == '\n' || last.back() == '\r')) {
        last.pop_back();
    }
    if (const auto newline = last.rfind('\n'); newline != std::string::npos) {
        last = last.substr(newline + 1);
    }
    std::string type = last;
    std::string text;
    if (const auto colon = last.find(": "); colon != std::string::npos) {
        type = last.substr(0, colon);
        text = last.substr(colon + 2);
    }
    std::smatch match;
    static const std::regex undefined(R"(name '(.+)' is not defined)");
    if (type == "NameError" && std::regex_search(text, match, undefined)) {
        error.message = tr("text.unknown_name", language, {{"name", match[1].str()}});
    } else if (type == "IndentationError") {
        error.message = tr("text.python_indentation", language);
    } else if (type == "SyntaxError") {
        error.message = tr("text.python_syntax", language, {{"message", text}});
    } else if (type == "ZeroDivisionError") {
        error.message = tr("text.division_by_zero", language);
    } else if (type == "ValueError" && py_matchexc(tp_ValueError)) {
        error.message = text; // the API's own messages are already in the user's language
    } else {
        error.message = text.empty() ? type : type + " : " + text;
    }
    py_clearexc(nullptr);
    return error;
}

} // namespace

std::optional<ScriptError> checkPython(const std::string& code, Language language) {
    const std::lock_guard<std::mutex> lock(pythonMutex());
    initialize();
    py_resetvm();
    if (!py_compile(code.c_str(), kFileName, EXEC_MODE, false)) {
        return takeError(language);
    }
    return std::nullopt;
}

namespace {

/// A fresh interpreter with the Arduino API, its output going to `state` (pythonMutex() held).
py_GlobalRef prepare(Running& state, Api& api) {
    initialize();
    py_resetvm();
    running = &state;
    py_callbacks()->print = printCallback;
    py_callbacks()->getchr = noInput;
    py_GlobalRef main = py_getmodule("__main__");
    bindApi(main, api);
    bindApi(py_newmodule("arduino"), api);
    // time.sleep() waits like delay(): Stop must not wait for it to end.
    if (!py_exec("import time\ntime.sleep = lambda seconds: delay(seconds * 1000)\ndel time\n", "<s2a>", EXEC_MODE,
                 nullptr)) {
        py_clearexc(nullptr); // no time module: nothing to replace
    }
    stopFlag = api.stopFlag();
    py_watchdog_begin(kWatchdogMs);
    return main;
}

void release(Running& state, Api& api) {
    if (!state.pending.empty()) {
        api.print(state.pending);
        state.pending.clear();
    }
    running = nullptr;
    stopFlag = nullptr;
    py_watchdog_end();
}

/// A Python value as a number for the blocks (True is 1, None or text is 0).
double toNumber(py_Ref value) {
    if (py_istype(value, tp_bool)) {
        return py_tobool(value) ? 1 : 0;
    }
    if (py_istype(value, tp_int)) {
        return static_cast<double>(py_toint(value));
    }
    if (py_istype(value, tp_float)) {
        return py_tofloat(value);
    }
    return 0;
}

/// Whole numbers stay integers: range(n), list indexes and print() need them ("13", not "13.0").
void newNumber(py_Ref slot, double value) {
    if (std::abs(value) < 9e15 && value == std::floor(value)) {
        py_newint(slot, static_cast<py_i64>(value));
    } else {
        py_newfloat(slot, value);
    }
}

class PythonModule final : public Module {
public:
    PythonModule(std::unique_ptr<Api> api, Language language)
        : Module(std::move(api)), lock_(pythonMutex()), language_(language) {
        state_.api = api_.get();
        state_.language = language;
        main_ = prepare(state_, *api_);
    }
    PythonModule(const PythonModule&) = delete;
    PythonModule& operator=(const PythonModule&) = delete;
    PythonModule(PythonModule&&) = delete;
    PythonModule& operator=(PythonModule&&) = delete;
    ~PythonModule() override { release(state_, *api_); }

    /// Runs the top-level statements of the code.
    bool start(const std::string& code, std::optional<ScriptError>& error) {
        if (py_exec(code.c_str(), kFileName, EXEC_MODE, nullptr)) {
            return true;
        }
        if (state_.stopped || api_->stopRequested()) {
            py_clearexc(nullptr);
            return false;
        }
        error = detail::takeError(language_);
        return false;
    }

    double call(const std::string& name, const std::vector<double>& arguments) override {
        if (failed()) {
            return 0;
        }
        py_ItemRef found = py_getdict(main_, py_name(name.c_str()));
        if (found == nullptr || !py_callable(found)) {
            fail({0, tr("validation.usercode_missing", language_, {{"name", name}}), {}});
            return 0;
        }
        py_watchdog_begin(kWatchdogMs);
        py_Ref function = py_pushtmp();
        py_assign(function, found);
        py_Ref first = nullptr;
        for (const double argument : arguments) {
            py_Ref slot = py_pushtmp();
            newNumber(slot, argument);
            if (first == nullptr) {
                first = slot;
            }
        }
        const bool ok = py_call(function, static_cast<int>(arguments.size()), first);
        py_shrink(static_cast<int>(arguments.size()) + 1);
        if (!ok) {
            failWithException();
            return 0;
        }
        return toNumber(py_retval());
    }

    double get(const std::string& name) override {
        py_ItemRef found = failed() ? nullptr : py_getdict(main_, py_name(name.c_str()));
        return found != nullptr ? toNumber(found) : 0;
    }

    void set(const std::string& name, double value) override {
        if (failed()) {
            return;
        }
        py_Ref slot = py_pushtmp();
        newNumber(slot, value);
        py_setdict(main_, py_name(name.c_str()), slot);
        py_pop();
    }

private:
    void failWithException() {
        if (state_.stopped || api_->stopRequested()) {
            py_clearexc(nullptr);
            return;
        }
        fail(detail::takeError(language_));
    }

    std::unique_lock<std::mutex> lock_;
    Language language_;
    Running state_;
    py_GlobalRef main_ = nullptr;
};

} // namespace

std::unique_ptr<Module> loadPython(const std::string& code, std::unique_ptr<Api> api, Language language,
                                   std::optional<ScriptError>& error) {
    auto module = std::make_unique<PythonModule>(std::move(api), language);
    if (!module->start(code, error)) {
        return nullptr;
    }
    return module;
}

std::optional<ScriptError> runPython(const std::string& code, Api& api, Language language) {
    const std::lock_guard<std::mutex> lock(pythonMutex());
    Running state;
    state.api = &api;
    state.language = language;
    py_GlobalRef main = prepare(state, api);
    struct Reset {
        Running& state;
        Api& api;
        ~Reset() { release(state, api); }
    } reset{state, api};

    bool ok = py_exec(code.c_str(), kFileName, EXEC_MODE, nullptr);
    // Arduino style: setup() once, then loop() again and again.
    if (ok) {
        py_ItemRef setup = py_getdict(main, py_name("setup"));
        if (setup != nullptr && py_callable(setup)) {
            py_Ref function = py_pushtmp();
            py_assign(function, setup);
            ok = py_call(function, 0, nullptr);
            py_pop();
        }
        py_ItemRef loop = py_getdict(main, py_name("loop"));
        while (ok && loop != nullptr && py_callable(loop) && !api.stopRequested()) {
            py_watchdog_begin(kWatchdogMs);
            py_Ref function = py_pushtmp();
            py_assign(function, loop);
            ok = py_call(function, 0, nullptr);
            py_pop();
            loop = py_getdict(main, py_name("loop"));
        }
    }
    if (!state.pending.empty()) {
        api.print(state.pending);
        state.pending.clear();
    }
    if (ok) {
        return std::nullopt;
    }
    if (state.stopped || api.stopRequested()) {
        py_clearexc(nullptr);
        return std::nullopt;
    }
    return takeError(language);
}

} // namespace s2a::script::detail
