// JavaScript mode: runs the program with QuickJS-ng (https://github.com/quickjs-ng/quickjs).
#include "s2a/script.hpp"

#include <quickjs.h>

#include <memory>
#include <regex>

namespace s2a::script::detail {

namespace {

constexpr const char* kFileName = "main.js";

/// What the functions of the API need, kept as the context's opaque pointer.
struct Running {
    Api* api = nullptr;
    Language language = Language::French;
    std::string apiMessage; ///< Last message of an ApiError: shown as is (already explained).
};

Running& running(JSContext* ctx) {
    return *static_cast<Running*>(JS_GetContextOpaque(ctx));
}

std::string toString(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    if (text == nullptr) {
        return {};
    }
    std::string result(text);
    JS_FreeCString(ctx, text);
    return result;
}

using Body = JSValue (*)(JSContext*, Running&, int, JSValueConst*);

/// Calls `body` and turns the API's C++ exceptions into JavaScript ones.
template <Body body> JSValue guarded(JSContext* ctx, JSValueConst /*self*/, int argc, JSValueConst* argv) {
    Running& state = running(ctx);
    try {
        return body(ctx, state, argc, argv);
    } catch (const ApiError& error) {
        state.apiMessage = error.message;
        return JS_ThrowRangeError(ctx, "%s", error.message.c_str());
    } catch (const Stopped&) {
        // The interrupt handler stops the program at once: this error cannot be caught.
        return JS_ThrowInternalError(ctx, "stopped");
    }
}

int pinArg(JSContext* ctx, Running& state, int argc, JSValueConst* argv, int index) {
    if (index >= argc) {
        throw ApiError{tr("text.argument_count", state.language)};
    }
    if (JS_IsString(argv[index])) {
        return state.api->pin(toString(ctx, argv[index]));
    }
    if (JS_IsNumber(argv[index])) {
        double value = 0;
        (void)JS_ToFloat64(ctx, &value, argv[index]);
        return state.api->pin(static_cast<long long>(value));
    }
    throw ApiError{tr("text.pin_expected", state.language)};
}

double numberArg(JSContext* ctx, Running& state, int argc, JSValueConst* argv, int index, std::optional<double> fallback = {}) {
    if (index >= argc || JS_IsUndefined(argv[index])) {
        if (fallback) {
            return *fallback;
        }
        throw ApiError{tr("text.argument_count", state.language)};
    }
    if (!JS_IsNumber(argv[index]) && !JS_IsBool(argv[index])) {
        throw ApiError{tr("text.number_expected", state.language)};
    }
    double value = 0;
    (void)JS_ToFloat64(ctx, &value, argv[index]);
    return value;
}

JSValue pinModeFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    s.api->pinMode(pinArg(ctx, s, argc, argv, 0), static_cast<int>(numberArg(ctx, s, argc, argv, 1)));
    return JS_UNDEFINED;
}
JSValue digitalWriteFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    s.api->digitalWrite(pinArg(ctx, s, argc, argv, 0), numberArg(ctx, s, argc, argv, 1) != 0);
    return JS_UNDEFINED;
}
JSValue digitalReadFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    return JS_NewInt32(ctx, s.api->digitalRead(pinArg(ctx, s, argc, argv, 0)));
}
JSValue analogReadFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    return JS_NewInt32(ctx, s.api->analogRead(pinArg(ctx, s, argc, argv, 0)));
}
JSValue analogWriteFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    s.api->analogWrite(pinArg(ctx, s, argc, argv, 0), numberArg(ctx, s, argc, argv, 1));
    return JS_UNDEFINED;
}
JSValue delayFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    s.api->delay(numberArg(ctx, s, argc, argv, 0));
    return JS_UNDEFINED;
}
JSValue millisFn(JSContext* ctx, Running& s, int /*argc*/, JSValueConst* /*argv*/) {
    s.api->checkStop();
    return JS_NewNumber(ctx, s.api->millis());
}
JSValue toneFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    s.api->tone(pinArg(ctx, s, argc, argv, 0), numberArg(ctx, s, argc, argv, 1), numberArg(ctx, s, argc, argv, 2, 0.0));
    return JS_UNDEFINED;
}
JSValue noToneFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    s.api->noTone(pinArg(ctx, s, argc, argv, 0));
    return JS_UNDEFINED;
}
JSValue servoWriteFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    s.api->servoWrite(pinArg(ctx, s, argc, argv, 0), numberArg(ctx, s, argc, argv, 1));
    return JS_UNDEFINED;
}
/// print(a, b) and console.log(a, b): one line, values separated by spaces.
JSValue printFn(JSContext* ctx, Running& s, int argc, JSValueConst* argv) {
    std::string line;
    for (int index = 0; index < argc; ++index) {
        if (index > 0) {
            line += ' ';
        }
        line += toString(ctx, argv[index]);
    }
    s.api->print(line);
    return JS_UNDEFINED;
}

struct Function {
    const char* name;
    JSCFunction* function;
    int length;
};

constexpr Function kFunctions[] = {
    {"pinMode", guarded<pinModeFn>, 2},
    {"digitalWrite", guarded<digitalWriteFn>, 2},
    {"digitalRead", guarded<digitalReadFn>, 1},
    {"analogRead", guarded<analogReadFn>, 1},
    {"analogWrite", guarded<analogWriteFn>, 2},
    {"delay", guarded<delayFn>, 1},
    {"millis", guarded<millisFn>, 0},
    {"tone", guarded<toneFn>, 3},
    {"noTone", guarded<noToneFn>, 1},
    {"servoWrite", guarded<servoWriteFn>, 2},
    {"print", guarded<printFn>, 1},
};

void bindApi(JSContext* ctx, JSValueConst target, const Api& api) {
    for (const Function& function : kFunctions) {
        JS_SetPropertyStr(ctx, target, function.name, JS_NewCFunction(ctx, function.function, function.name, function.length));
    }
    for (const auto& [name, value] : api.constants()) {
        JS_SetPropertyStr(ctx, target, name.c_str(), JS_NewInt32(ctx, value));
    }
}

int interrupt(JSRuntime* /*runtime*/, void* opaque) {
    return static_cast<const Api*>(opaque)->stopRequested() ? 1 : 0;
}

/// The line of the user's program in an error's stack ("at loop (main.js:3:5)").
int lineOf(const std::string& stack) {
    static const std::regex pattern(R"(main\.js:(\d+))");
    std::smatch match;
    return std::regex_search(stack, match, pattern) ? std::stoi(match[1].str()) : 0;
}

/// The pending exception, explained.
ScriptError takeError(JSContext* ctx, const Running& state, Language language) {
    JSValue exception = JS_GetException(ctx);
    ScriptError error;
    std::string name;
    std::string message;
    if (JS_IsError(exception)) {
        JSValue value = JS_GetPropertyStr(ctx, exception, "name");
        name = toString(ctx, value);
        JS_FreeValue(ctx, value);
        value = JS_GetPropertyStr(ctx, exception, "message");
        message = toString(ctx, value);
        JS_FreeValue(ctx, value);
        value = JS_GetPropertyStr(ctx, exception, "stack");
        const std::string stack = JS_IsUndefined(value) ? std::string{} : toString(ctx, value);
        JS_FreeValue(ctx, value);
        value = JS_GetPropertyStr(ctx, exception, "lineNumber");
        double line = 0;
        if (JS_IsNumber(value)) {
            (void)JS_ToFloat64(ctx, &line, value);
        }
        JS_FreeValue(ctx, value);
        error.line = lineOf(stack);
        if (error.line == 0) {
            error.line = static_cast<int>(line);
        }
        error.details = name + ": " + message + "\n" + stack;
    } else {
        message = toString(ctx, exception); // throw "text"
        error.details = message;
    }
    JS_FreeValue(ctx, exception);

    std::smatch match;
    static const std::regex undefinedName(R"(^'?(\w+)'? is not defined$)");
    if (!state.apiMessage.empty() && message == state.apiMessage) {
        error.message = message; // the API's own messages are already in the user's language
    } else if (name == "ReferenceError" && std::regex_match(message, match, undefinedName)) {
        error.message = tr("text.unknown_name", language, {{"name", match[1].str()}});
    } else if (name == "SyntaxError") {
        error.message = tr("text.javascript_syntax", language, {{"message", message}});
    } else {
        error.message = name.empty() ? message : name + " : " + message;
    }
    return error;
}

struct Engine {
    JSRuntime* runtime = JS_NewRuntime();
    JSContext* context = runtime != nullptr ? JS_NewContext(runtime) : nullptr;
    Engine() {
        if (runtime != nullptr) {
            JS_SetMemoryLimit(runtime, 64U * 1024U * 1024U);
        }
    }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
    ~Engine() {
        if (context != nullptr) {
            JS_FreeContext(context);
        }
        if (runtime != nullptr) {
            JS_FreeRuntime(runtime);
        }
    }
};

} // namespace

std::optional<ScriptError> checkJavaScript(const std::string& code, Language language) {
    Engine engine;
    if (engine.context == nullptr) {
        return ScriptError{0, tr("error.unknown", language), "QuickJS could not start"};
    }
    Running state;
    state.language = language;
    JS_SetContextOpaque(engine.context, &state);
    JSValue compiled = JS_Eval(engine.context, code.c_str(), code.size(), kFileName,
                               JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) {
        return takeError(engine.context, state, language);
    }
    JS_FreeValue(engine.context, compiled);
    return std::nullopt;
}

std::optional<ScriptError> runJavaScript(const std::string& code, Api& api, Language language) {
    Engine engine;
    if (engine.context == nullptr) {
        return ScriptError{0, tr("error.unknown", language), "QuickJS could not start"};
    }
    JSContext* ctx = engine.context;
    Running state;
    state.api = &api;
    state.language = language;
    JS_SetContextOpaque(ctx, &state);
    JS_SetInterruptHandler(engine.runtime, interrupt, &api);

    JSValue global = JS_GetGlobalObject(ctx);
    bindApi(ctx, global, api);
    JSValue arduino = JS_NewObject(ctx);
    bindApi(ctx, arduino, api);
    JS_SetPropertyStr(ctx, global, "arduino", arduino);
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, guarded<printFn>, "log", 1));
    JS_SetPropertyStr(ctx, global, "console", console);

    JSValue result = JS_Eval(ctx, code.c_str(), code.size(), kFileName, JS_EVAL_TYPE_GLOBAL);
    bool ok = !JS_IsException(result);
    JS_FreeValue(ctx, result);
    // Arduino style: setup() once, then loop() again and again.
    if (ok) {
        JSValue setup = JS_GetPropertyStr(ctx, global, "setup");
        if (JS_IsFunction(ctx, setup)) {
            result = JS_Call(ctx, setup, JS_UNDEFINED, 0, nullptr);
            ok = !JS_IsException(result);
            JS_FreeValue(ctx, result);
        }
        JS_FreeValue(ctx, setup);
    }
    while (ok && !api.stopRequested()) {
        JSValue loop = JS_GetPropertyStr(ctx, global, "loop");
        if (!JS_IsFunction(ctx, loop)) {
            JS_FreeValue(ctx, loop);
            break;
        }
        result = JS_Call(ctx, loop, JS_UNDEFINED, 0, nullptr);
        ok = !JS_IsException(result);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, loop);
    }
    std::optional<ScriptError> error;
    if (!ok && !api.stopRequested()) {
        error = takeError(ctx, state, language);
    } else if (!ok) {
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
    JS_FreeValue(ctx, global);
    return error;
}

} // namespace s2a::script::detail
