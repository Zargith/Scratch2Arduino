#include "s2a/arduino_cli.hpp"
#include "s2a/board_packages.hpp"
#include "s2a/boards.hpp"
#include "s2a/build.hpp"
#include "s2a/codegen.hpp"
#include "s2a/convert.hpp"
#include "s2a/diagnostics.hpp"
#include "s2a/integrity.hpp"
#include "s2a/live.hpp"
#include "s2a/network.hpp"
#include "s2a/process.hpp"
#include "s2a/project.hpp"
#include "s2a/rosetta.hpp"
#include "s2a/simulator.hpp"
#include "s2a/usercode.hpp"
#include "s2a/version.hpp"
#include "s2a/wiring.hpp"

#include "bridge.hpp"
#include "bridge_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>
#include <nlohmann/json.hpp>
#include <portable-file-dialogs.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h, which they need.
#include <objbase.h>
#include <shellapi.h>
#include <urlmon.h>
#endif

namespace s2a::webview {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
// Every dump() below replaces bytes that are not UTF-8 (a Latin-1 file name, board output, compiler
// text) instead of throwing, which failed the call or, inside a catch handler, ended the program
// (audit #187).

/// Thrown by a call to reject its promise with a message for the user.
struct UserError {
    std::string message;
};

/// Rejection of a call that failed unexpectedly: a friendly message for the child, the
/// technical details on stderr for whoever helps.
std::string internalError(const std::exception& error, Language language) {
    std::cerr << "Scratch2Arduino: unexpected error in a call from the editor: " << error.what() << '\n';
    return Json(tr("error.unknown", language)).dump(-1, ' ', false, Json::error_handler_t::replace);
}

Language languageArg(const Json& args, std::size_t index, Language fallback) {
    return index < args.size() && args[index].is_string() ? languageFromLocale(args[index].get<std::string>())
                                                         : fallback;
}

project::Project projectArg(const Json& args, std::size_t index, Language language) {
    if (index >= args.size()) {
        throw UserError{tr(errorMessageKey(ErrorCode::InvalidJson), language)};
    }
    auto parsed = project::fromJson(args[index].dump(-1, ' ', false, Json::error_handler_t::replace));
    if (!parsed.ok()) {
        throw UserError{tr(errorMessageKey(parsed.error().code), language) + " (" + parsed.error().detail + ")"};
    }
    return std::move(parsed).value();
}

std::string stringArg(const Json& args, std::size_t index) {
    return index < args.size() && args[index].is_string() ? args[index].get<std::string>() : std::string{};
}

/// Documents/Scratch2Arduino, created on first use: where dialogs open by default.
fs::path projectsDirectory() {
#ifdef _WIN32
    const fs::path home = process::environmentPath("USERPROFILE");
#else
    const fs::path home = process::environmentPath("HOME");
#endif
    std::error_code error;
    fs::path documents = home / "Documents";
    if (!fs::is_directory(documents, error)) {
        documents = home;
    }
    const fs::path folder = documents / "Scratch2Arduino";
    fs::create_directories(folder, error);
    return error ? documents : folder;
}

/// Opens a page of the project's repository (a release, its installer) in the user's browser.
/// Only those pages: the editor cannot make the program open anything else.
bool openProjectPage(const std::string& url) {
    if (!detail::isProjectPage(url)) {
        return false;
    }
#ifdef _WIN32
    const std::wstring wide(url.begin(), url.end()); // ASCII only, checked above
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
#ifdef __APPLE__
    const char* opener = "open";
#else
    const char* opener = "xdg-open";
#endif
    process::Command command;
    command.program = opener;
    command.args = {url};
    command.timeout = std::chrono::seconds(15);
    const auto result = process::run(command);
    return result.ok() && result.value().succeeded();
#endif
}

/// Which release file suits this computer (frontend/src/updates.ts): "windows", "macos-arm64"...
std::string_view hostSystemName() {
    switch (toolchain::currentHost()) {
    case toolchain::HostSystem::WindowsX64:
        return "windows";
    case toolchain::HostSystem::MacArm64:
        return "macos-arm64";
    case toolchain::HostSystem::MacX64:
        return "macos-x86_64";
    case toolchain::HostSystem::LinuxX64:
    case toolchain::HostSystem::LinuxArm64:
        return "linux";
    }
    return "linux";
}

#ifdef _WIN32
/// COM for the calling thread (the bridge's worker threads), released at the end of the scope.
struct ComScope {
    bool ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    ComScope() = default;
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ~ComScope() {
        if (ok) {
            CoUninitialize();
        }
    }
};

/// True when `file` holds a Windows program (starts with "MZ"), not an HTML error page.
bool isProgramFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    char magic[2] = {};
    return in.read(magic, 2) && magic[0] == 'M' && magic[1] == 'Z';
}

/// Downloads a file of a release to `target`. First with Windows' own downloader (system proxy,
/// certificate checks as in the browser), then with curl.exe, which fails on many networks when
/// the certificate revocation server cannot be reached (hence --ssl-revoke-best-effort).
/// `accept` checks what Windows' downloader gave (an HTML error page is not the file).
bool downloadReleaseFile(const std::string& url, const fs::path& target, bool (*accept)(const fs::path&)) {
    std::error_code error;
    fs::remove(target, error);
    {
        const ComScope com;
        const std::wstring wideUrl(url.begin(), url.end()); // ASCII only, checked by isReleaseInstaller
        if (SUCCEEDED(URLDownloadToFileW(nullptr, wideUrl.c_str(), target.wstring().c_str(), 0, nullptr)) &&
            accept(target)) {
            return true;
        }
    }
    std::cerr << "Scratch2Arduino: URLDownloadToFile failed, trying curl.exe\n";
    fs::remove(target, error);
    process::Command download;
    download.program = "curl.exe"; // part of Windows 10 and 11
    download.args = {"--location", "--proto", "=https", "--proto-redir", "=https", "--fail", "--silent",
                     "--show-error", "--ssl-revoke-best-effort",
                     "--retry", "2", "--output", target.u8string(), url};
    download.timeout = std::chrono::minutes(15);
    const auto result = process::run(download);
    if (!result.ok() || !result.value().succeeded()) {
        std::cerr << "Scratch2Arduino: curl.exe failed: "
                  << (result.ok() ? result.value().err : result.error().detail) << '\n';
        return false;
    }
    return accept(target);
}

/// A SHA256SUMS.txt: a small file (a few lines), not an error page or something else.
bool smallFile(const fs::path& file) {
    std::error_code error;
    return fs::is_regular_file(file, error) && fs::file_size(file, error) <= 64 * 1024 && !error;
}

/// Removes the folders of earlier updates (the installer they hold is no longer needed).
void removeOldUpdateFolders(const fs::path& temp) {
    std::error_code error;
    for (fs::directory_iterator it(temp, error), end; !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().u8string();
        if (name.rfind("Scratch2Arduino-update", 0) == 0) {
            std::error_code ignored;
            fs::remove_all(it->path(), ignored);
        }
    }
}

/// Downloads the installer at `url` into `folder` and checks it against the SHA256SUMS.txt of the
/// same release (audit #174): a truncated, cached or swapped file is never run as administrator.
std::optional<fs::path> downloadVerifiedInstaller(const std::string& url, const fs::path& folder) {
    const std::size_t slash = url.rfind('/');
    const std::string fileName = url.substr(slash + 1);
    const fs::path installer = folder / "Scratch2Arduino_installer.exe";
    const fs::path sumsFile = folder / "SHA256SUMS.txt";
    if (!downloadReleaseFile(url.substr(0, slash + 1) + "SHA256SUMS.txt", sumsFile, smallFile) ||
        !downloadReleaseFile(url, installer, isProgramFile)) {
        return std::nullopt;
    }
    std::ifstream in(sumsFile, std::ios::binary);
    const std::string sums((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (const auto verified = integrity::verifyFile(installer, sums, fileName); !verified.ok()) {
        std::cerr << "Scratch2Arduino: the downloaded installer is refused: " << verified.error().detail << '\n';
        std::error_code error;
        fs::remove(installer, error);
        return std::nullopt;
    }
    return installer;
}

/// Starts the downloaded installer (Windows asks for the administrator rights).
bool startInstaller(const fs::path& installer) {
    const ComScope com; // ShellExecuteEx needs COM on the calling thread
    const std::wstring file = installer.wstring();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = file.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info) == FALSE) {
        std::cerr << "Scratch2Arduino: cannot start the installer, error " << GetLastError() << '\n';
        return false;
    }
    return true;
}
#endif

/// Wiring check of the Live mode: [{message, blockIds}].
Json wiringHints(const std::vector<live::WiringHint>& hints) {
    Json list = Json::array();
    for (const auto& hint : hints) {
        list.push_back({{"message", hint.message}, {"blockIds", hint.blockIds}});
    }
    return list;
}

/// Live output for the editor: [{text, error}] (error: printError, console.error, the error that stopped it).
Json outputLines(const std::vector<live::Session::OutputLine>& lines) {
    Json list = Json::array();
    for (const auto& line : lines) {
        list.push_back({{"text", line.text}, {"error", line.error}});
    }
    return list;
}

toolchain::ArduinoCli arduinoCli(Language language) {
    const fs::path executable = toolchain::locateArduinoCli();
    if (executable.empty()) {
        throw UserError{tr("cli.arduino_cli_missing", language)};
    }
    return toolchain::ArduinoCli(executable, toolchain::Directories::defaults());
}

std::string outcomeJson(const toolchain::BuildResult& result) {
    Json problems = Json::array();
    for (const toolchain::CodeProblem& problem : result.problems) {
        problems.push_back(
            {{"line", problem.line}, {"column", problem.column}, {"message", problem.message}, {"file", problem.file}});
    }
    return Json{{"ok", result.ok},
                {"problems", std::move(problems)},
                {"message", result.message},
                {"details", result.details},
                {"blockIds", result.blockIds},
                {"warnings", result.warnings},
                {"warningBlockIds", result.warningBlockIds}}
        .dump(-1, ' ', false, Json::error_handler_t::replace);
}

} // namespace

Bridge::Bridge(HostWindow& window, Language language)
    : window_(window), language_(language), user_(userdata::defaultDirectory()) {
    // The language chosen in the settings wins over the system's.
    const std::string chosen = user_.settings().language;
    if (chosen == "fr") {
        language_ = Language::French;
    } else if (chosen == "en") {
        language_ = Language::English;
    }
}

namespace {

/// What the editor draws of the virtual board (see sim::VirtualBoard::State).
std::string simulationJson(const sim::VirtualBoard::State& state) {
    Json outputs = Json::object();
    for (const auto& [pin, output] : state.outputs) {
        outputs[pin] = {{"level", output.level}, {"pwm", output.pwm}};
    }
    Json climates = Json::object();
    for (const auto& [pin, climate] : state.climates) {
        climates[pin] = {climate.first, climate.second};
    }
    return Json{{"modes", state.modes},
                {"outputs", outputs},
                {"servos", state.servos},
                {"steppers", state.steppers},
                {"digits", state.digits},
                {"tones", state.tones},
                {"lcd", state.lcd},
                {"lcd16", state.lcd16},
                {"oled", state.oled},
                {"pixels", state.pixels},
                {"matrices", state.matrices},
                {"phone", state.phone},
                {"pressed", state.pressed},
                {"analog", state.analog},
                {"distances", state.distances},
                {"climates", climates}}
        .dump(-1, ' ', false, Json::error_handler_t::replace);
}

const char* stateName(live::Session::State state) {
    switch (state) {
    case live::Session::State::Connecting:
        return "connecting";
    case live::Session::State::Flashing:
        return "flashing";
    case live::Session::State::Running:
        return "running";
    case live::Session::State::Finished:
        return "finished";
    case live::Session::State::Stopped:
        return "stopped";
    case live::Session::State::NeedsFirmware:
        return "needs_firmware";
    case live::Session::State::Failed:
        return "failed";
    }
    return "stopped";
}

} // namespace

void Bridge::closeMonitor() {
    std::unique_ptr<serial::Monitor> monitor;
    {
        const std::lock_guard<std::mutex> lock(monitorMutex_);
        monitor.swap(monitor_);
    }
    monitor.reset(); // joins the reading thread (at most one read timeout)
}

void Bridge::retireLive() {
    if (!live_) {
        return;
    }
    live_->stop(); // the old blocks stop driving the board at once
    std::unique_ptr<live::Session> old = std::move(live_);
    try {
        std::shared_future<void> previous = liveClosing_;
        liveClosing_ = std::async(std::launch::async,
                                  [session = std::move(old), previous]() mutable {
                                      if (previous.valid()) {
                                          previous.wait(); // one teardown at a time on the port
                                          previous = {};   // the chain of stopped sessions is not kept
                                      }
                                      session.reset(); // switches the outputs off, closes the port
                                  })
                           .share();
    } catch (const std::system_error&) {
        old.reset(); // no thread left: stop it here
    }
}

void Bridge::waitForLiveClosed() {
    std::shared_future<void> closing;
    {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        closing = liveClosing_;
    }
    if (closing.valid()) {
        closing.wait();
    }
}

Bridge::~Bridge() {
    // The port calls still queued run first, so that none opens a port after the cleanup below.
    {
        const std::lock_guard<std::mutex> lock(portMutex_);
        portStopping_ = true;
    }
    portReady_.notify_one();
    if (portThread_.joinable()) {
        portThread_.join();
    }
    closeMonitor();
    {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        retireLive();
    }
    waitForLiveClosed(); // the outputs are off before the program ends
    std::list<Worker> workers;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        workers.swap(workers_);
    }
    for (Worker& worker : workers) {
        worker.thread.join();
    }
}

std::unique_ptr<serial::SerialPort> Bridge::openLiveOverUsb(const Board& board, const std::string& port,
                                                            Language language) {
    closeMonitor();
    {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        retireLive(); // the port is needed
    }
    waitForLiveClosed();
    const auto failure = [&](const Error& error) { return UserError{tr(errorMessageKey(error.code), language)}; };
    auto opened = serial::SerialPort::open(port, live::kBaud);
    if (!opened.ok()) {
        throw failure(opened.error());
    }
    std::unique_ptr<serial::SerialPort> connection = std::move(opened).value();
    // A slow bootloader (Mega, some clones) gets a second chance before the firmware is installed.
    if (live::LiveLink(*connection).handshake(std::chrono::milliseconds(3500)).ok() ||
        live::LiveLink(*connection).handshake(std::chrono::milliseconds(4000)).ok()) {
        return connection;
    }
    connection.reset(); // no Live firmware yet: install it, then connect again
    std::string details;
    if (const auto flashed = live::Session::arduinoCliFlasher(board, port)(details); !flashed.ok()) {
        throw failure(flashed.error());
    }
    opened = serial::SerialPort::open(port, live::kBaud);
    if (!opened.ok()) {
        throw failure(opened.error());
    }
    connection = std::move(opened).value();
    if (const auto ready = live::LiveLink(*connection).handshake(); !ready.ok()) {
        throw failure(ready.error());
    }
    return connection;
}

template <typename Work> void Bridge::async(const std::string& id, Work work) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = workers_.begin(); it != workers_.end();) {
        if (it->done->load()) {
            it->thread.join();
            it = workers_.erase(it);
        } else {
            ++it;
        }
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread thread([this, id, done, task = std::move(work)]() {
        try {
            window_.resolve(id, true, task());
        } catch (const UserError& error) {
            window_.resolve(id, false, Json(error.message).dump(-1, ' ', false, Json::error_handler_t::replace));
        } catch (const std::exception& error) {
            window_.resolve(id, false, internalError(error, language_));
        }
        done->store(true);
    });
    workers_.push_back({std::move(thread), std::move(done)});
}

template <typename Handler> void Bridge::bindPortCall(const std::string& name, Handler handler) {
    // Opening a port (which resets an Uno) or starting a Live session can take seconds on a slow
    // USB or Bluetooth port: not on the UI thread (audit #194). One thread, in call order, so that
    // a close never overtakes the open before it and the monitor and Live never share the port.
    window_.bind(
        name,
        [this, handler = std::move(handler)](const std::string& id, const std::string& request) {
            {
                const std::lock_guard<std::mutex> lock(portMutex_);
                portJobs_.push_back([this, handler, id, request]() {
                    try {
                        window_.resolve(id, true, handler(request));
                    } catch (const UserError& error) {
                        window_.resolve(id, false, Json(error.message).dump(-1, ' ', false, Json::error_handler_t::replace));
                    } catch (const std::exception& error) {
                        window_.resolve(id, false, internalError(error, language_));
                    }
                });
                if (!portThread_.joinable()) {
                    portThread_ = std::thread([this] { runPortQueue(); });
                }
            }
            portReady_.notify_one();
        });
}

void Bridge::runPortQueue() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(portMutex_);
            portReady_.wait(lock, [this] { return portStopping_ || !portJobs_.empty(); });
            if (portJobs_.empty()) {
                return; // stopping, and nothing left
            }
            job = std::move(portJobs_.front());
            portJobs_.pop_front();
        }
        job();
    }
}

template <typename Handler> void Bridge::bindSync(const std::string& name, Handler handler) {
    // webview's own synchronous bind lets exceptions escape through its C callback, which ends
    // the program (std::terminate on GTK and Cocoa): unsaved blocks would be lost (audit #168).
    window_.bind(
        name,
        [this, handler = std::move(handler)](const std::string& id, const std::string& request) {
            try {
                window_.resolve(id, true, handler(request));
            } catch (const UserError& error) {
                window_.resolve(id, false, Json(error.message).dump(-1, ' ', false, Json::error_handler_t::replace));
            } catch (const std::exception& error) {
                window_.resolve(id, false, internalError(error, language_));
            }
        });
}

std::string Bridge::openedJson(const fs::path& path, Language language) {
    auto loaded = project::load(path);
    if (!loaded.ok()) {
        throw UserError{tr(errorMessageKey(loaded.error().code), language)};
    }
    {
        const std::lock_guard<std::mutex> lock(userMutex_);
        (void)user_.addRecentProject(path); // a convenience: never a reason to fail
    }
    // A project.json (an example) is never overwritten: the first save asks for a .s2a file.
    const std::string savePath = path.extension() == ".s2a" ? path.u8string() : std::string{};
    return Json{{"path", savePath}, {"name", path.u8string()}, {"project", Json::parse(project::toJson(loaded.value()))}}
        .dump(-1, ' ', false, Json::error_handler_t::replace);
}

/// File chosen in a save dialog, with `extension` added when the user typed none, or empty when
/// cancelled. The dialog asks before replacing a file it shows; a name typed without the
/// extension is checked here, once the extension is added (audit #192: "robot" replaced robot.s2a).
fs::path chooseSaveFile(const std::string& title, const fs::path& suggestion, const std::string& filterName,
                        const std::string& extension, Language language) {
    const std::string chosen =
        pfd::save_file(title, suggestion.u8string(), {filterName, "*" + extension}).result();
    if (chosen.empty()) {
        return {};
    }
    fs::path file = fs::u8path(chosen);
    std::string typed = file.extension().u8string();
    std::transform(typed.begin(), typed.end(), typed.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (typed == extension) { // "ROBOT.S2A" is already a project name, not "ROBOT.S2A.s2a"
        return file;
    }
    file += extension;
    std::error_code error;
    if (fs::exists(file, error) &&
        pfd::message(title, tr("webview.replace_file", language, {{"name", file.filename().u8string()}}),
                     pfd::choice::yes_no, pfd::icon::question)
                .result() != pfd::button::yes) {
        return {};
    }
    return file;
}

void Bridge::bind() {
    bindSync("s2a_hello", [this](const std::string& /*request*/) {
        return Json{{"language", std::string(languageCode(language_))},
                    {"version", std::string(version::kString)},
                    {"system", std::string(hostSystemName())}}
            .dump(-1, ' ', false, Json::error_handler_t::replace);
    });

    bindSync("s2a_set_title", [this](const std::string& request) {
        window_.setTitle(stringArg(Json::parse(request), 0));
        return std::string{};
    });

    window_.bind(
        "s2a_generate",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const project::Project project = projectArg(args, 0, language_);
                return Json(codegen::generate(project).source).dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    // Where the expert C++ mode starts from: "My code" (in C++) followed by the code of the blocks.
    window_.bind(
        "s2a_expert_sketch",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const project::Project project = projectArg(args, 0, language_);
                return Json(codegen::singleFileSketch(project)).dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    // Functions and variables of a "My code" file, for its palette category: (language, code).
    // Quick and never failing: called each time the user goes back to the blocks.
    // Rewrites a program in another text language: (code, from, to) -> {code, toCheck: [lines]}.
    // Never fails: what it does not understand comes back as comments, their lines in toCheck.
    bindSync("s2a_convert", [](const std::string& request) {
        const Json args = Json::parse(request);
        const auto from = project::programLanguageFromId(stringArg(args, 1));
        const auto to = project::programLanguageFromId(stringArg(args, 2));
        const std::string code = stringArg(args, 0);
        if (!from || !to) {
            return Json{{"code", code}, {"toCheck", Json::array()}}.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        const auto converted = convert::convert(code, *from, *to);
        return Json{{"code", converted.code}, {"toCheck", converted.toCheck}}.dump(-1, ' ', false, Json::error_handler_t::replace);
    });

    bindSync("s2a_code_declarations", [](const std::string& request) {
        const Json args = Json::parse(request);
        const auto language = project::programLanguageFromId(stringArg(args, 0));
        Json list = Json::array();
        if (!language) {
            return list.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        for (const auto& declaration : usercode::declarations(*language, stringArg(args, 1))) {
            list.push_back({{"kind", declaration.kind == usercode::Declaration::Kind::Function ? "function" : "variable"},
                            {"name", declaration.name},
                            {"parameters", declaration.parameters},
                            {"returnsValue", declaration.returnsValue},
                            {"readOnly", declaration.readOnly},
                            {"changedByCode", declaration.changedByCode},
                            {"line", declaration.line}});
        }
        return list.dump(-1, ' ', false, Json::error_handler_t::replace);
    });

    // Wiring of the project (components, connections, warnings), from its blocks.
    window_.bind(
        "s2a_wiring",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 1, language_);
                const project::Project project = projectArg(args, 0, language);
                const wiring::Plan plan = wiring::plan(project, language);
                const Board* board = BoardDatabase::builtin().find(project.boardId);
                Json components = Json::array();
                for (const auto& component : plan.components) {
                    Json connections = Json::array();
                    for (const auto& connection : component.connections) {
                        connections.push_back({{"componentPin", connection.componentPin},
                                               {"boardPin", connection.boardPin},
                                               {"via", connection.via},
                                               {"role", connection.role}});
                    }
                    components.push_back({{"kind", component.kind},
                                          {"name", component.name},
                                          {"connections", connections},
                                          {"note", component.note},
                                          {"blockIds", component.blockIds}});
                }
                Json warnings = Json::array();
                for (const auto& warning : plan.warnings) {
                    warnings.push_back({{"message", warning.message}, {"blockIds", warning.blockIds}});
                }
                Json result{{"components", components}, {"warnings", warnings}};
                if (board != nullptr) {
                    result["svg"] = wiring::svg(plan, *board, project.name + " - " + board->name);
                }
                return result.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    // A picture made by the editor (the wiring diagram as PNG), saved where the user chooses.
    window_.bind(
        "s2a_save_image",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() -> std::string {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 2, language_);
                const std::string name = stringArg(args, 0);
                const std::string encoded = stringArg(args, 1);
                constexpr std::size_t kMaxEncodedImage = 64U * 1024U * 1024U; // a diagram is far smaller
                if (encoded.size() > kMaxEncodedImage) {
                    throw UserError{tr(errorMessageKey(ErrorCode::WriteFailed), language)};
                }
                const std::string data = detail::base64Decode(encoded);
                const fs::path suggestion =
                    projectsDirectory() / (codegen::identifier(name.empty() ? "schema" : name) + ".png");
                const fs::path file = chooseSaveFile(tr("webview.image_title", language), suggestion,
                                                     tr("webview.image_filter", language), ".png", language);
                if (file.empty()) {
                    return "null";
                }
                if (const auto written = userdata::writeFileAtomically(file, data); !written.ok()) {
                    throw UserError{tr(errorMessageKey(ErrorCode::WriteFailed), language)};
                }
                return Json(file.u8string()).dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    window_.bind(
        "s2a_open",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() -> std::string {
                const Language language = languageArg(Json::parse(request), 0, language_);
                const auto files = pfd::open_file(tr("webview.open_title", language), projectsDirectory().u8string(),
                                                  {tr("webview.file_filter", language), "*.s2a *.json"})
                                       .result();
                if (files.empty()) {
                    return "null";
                }
                return openedJson(fs::u8path(files.front()), language);
            });
        });
    // Project given on the command line, handed to the editor once (null afterwards).
    window_.bind(
        "s2a_startup_project",
        [this](const std::string& id, const std::string& request) {
            fs::path path;
            std::swap(path, startupFile_);
            async(id, [request, path, this]() -> std::string {
                if (path.empty()) {
                    return "null";
                }
                return openedJson(path, languageArg(Json::parse(request), 0, language_));
            });
        });

    window_.bind(
        "s2a_save",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() -> std::string {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 2, language_);
                project::Project project = projectArg(args, 0, language);
                // Optional picture of the project (PNG data URL); one that is too big is left out.
                if (args.size() > 3 && args[3].is_string()) {
                    std::string thumbnail = detail::base64Decode(args[3].get<std::string>());
                    if (thumbnail.size() <= project::kMaxThumbnailSize) {
                        project.thumbnail = std::move(thumbnail);
                    }
                }
                fs::path path = fs::u8path(stringArg(args, 1));
                if (path.empty()) {
                    const fs::path suggestion =
                        projectsDirectory() / (codegen::identifier(project.name.empty() ? "projet" : project.name) + ".s2a");
                    path = chooseSaveFile(tr("webview.save_title", language), suggestion,
                                          tr("webview.file_filter", language), ".s2a", language);
                    if (path.empty()) {
                        return "null";
                    }
                }
                if (const auto saved = project::save(project, path); !saved.ok()) {
                    throw UserError{tr(errorMessageKey(saved.error().code), language)};
                }
                {
                    const std::lock_guard<std::mutex> lock(userMutex_);
                    (void)user_.addRecentProject(path);
                    user_.clearAutosave(); // the work is safe in its own file now
                }
                return Json(path.u8string()).dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    // Settings of the editor: {language ("fr", "en" or "" for the system's), checkUpdates}.
    bindSync("s2a_settings", [this](const std::string& /*request*/) {
        userdata::Settings settings;
        {
            const std::lock_guard<std::mutex> lock(userMutex_);
            settings = user_.settings();
        }
        return Json{{"language", settings.language}, {"checkUpdates", settings.checkUpdates}, {"theme", settings.theme}}
            .dump(-1, ' ', false, Json::error_handler_t::replace);
    });
    bindSync("s2a_save_settings", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const Json given = args.is_array() && !args.empty() && args[0].is_object() ? args[0] : Json::object();
        userdata::Settings settings;
        settings.language = given.value("language", std::string{});
        settings.checkUpdates = given.value("checkUpdates", true);
        settings.theme = given.value("theme", std::string{});
        const std::lock_guard<std::mutex> lock(userMutex_);
        return Json(user_.saveSettings(settings).ok()).dump(-1, ' ', false, Json::error_handler_t::replace);
    });
    // Opens a page of the project's GitHub repository (new release) in the browser.
    window_.bind(
        "s2a_open_url",
        [this](const std::string& id, const std::string& request) {
            async(id, [request]() { return Json(openProjectPage(stringArg(Json::parse(request), 0))).dump(-1, ' ', false, Json::error_handler_t::replace); });
        });

    // Rosetta (Mac with an Apple chip): the board compilers need it. State: {missing, message};
    // install: {ok, message} once macOS's password prompt and Apple's download are done.
    window_.bind(
        "s2a_rosetta_state",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Language language = languageArg(Json::parse(request), 0, language_);
                const bool missing = rosetta::missing();
                return Json{{"missing", missing},
                            {"message", missing ? tr("rosetta.needed", language,
                                                     {{"command", std::string(rosetta::kInstallCommand)}})
                                                : std::string{}}}
                    .dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });
    window_.bind(
        "s2a_rosetta_install",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Language language = languageArg(Json::parse(request), 0, language_);
                const auto result = rosetta::install();
                return Json{{"ok", result == rosetta::InstallResult::Installed},
                            {"message", tr(rosetta::messageKey(result), language,
                                           {{"command", std::string(rosetta::kInstallCommand)}})}}
                    .dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    // Update from the settings (Windows): downloads the installer of a release, starts it and
    // closes the program so that the installer can replace it. Resolves to false elsewhere.
    window_.bind(
        "s2a_install_update",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const std::string url = stringArg(args, 0);
                const Language language = languageArg(args, 1, language_);
#ifdef _WIN32
                if (!detail::isReleaseInstaller(url)) {
                    throw UserError{tr("webview.update_failed", language)};
                }
                // A new folder for each update: no other program can prepare a file at a known path.
                std::error_code error;
                const fs::path temp = fs::temp_directory_path(error);
                if (error || temp.empty()) {
                    throw UserError{tr("webview.update_failed", language)};
                }
                removeOldUpdateFolders(temp);
                const fs::path folder = temp / ("Scratch2Arduino-update-" + project::newId());
                if (!fs::create_directories(folder, error) || error) {
                    throw UserError{tr("webview.update_failed", language)};
                }
                const auto installer = downloadVerifiedInstaller(url, folder);
                if (!installer || !startInstaller(*installer)) {
                    fs::remove_all(folder, error); // the browser downloads it instead
                    throw UserError{tr("webview.update_failed", language)};
                }
                // The installer waits for the program to close (AppMutex): close it.
                window_.dispatch([this]() { window_.terminate(); });
                return Json(true).dump(-1, ' ', false, Json::error_handler_t::replace);
#else
                // macOS and Linux: the editor opens the .dmg or the release page in the browser.
                (void)url;
                (void)language;
                return Json(false).dump(-1, ' ', false, Json::error_handler_t::replace);
#endif
            });
        });

    // Recent projects, newest first: [{path, name, thumbnail?}] (thumbnail: PNG data URL).
    window_.bind(
        "s2a_recent",
        [this](const std::string& id, const std::string& /*request*/) {
            async(id, [this]() {
                std::vector<fs::path> paths;
                {
                    const std::lock_guard<std::mutex> lock(userMutex_);
                    paths = user_.recentProjects();
                }
                Json list = Json::array();
                for (const fs::path& path : paths) {
                    Json entry{{"path", path.u8string()}, {"name", path.stem().u8string()}};
                    if (const std::string thumbnail = project::thumbnailOf(path); !thumbnail.empty()) {
                        entry["thumbnail"] = "data:image/png;base64," + detail::base64Encode(thumbnail);
                    }
                    list.push_back(std::move(entry));
                }
                return list.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });
    // Reopens a recent project. Only paths of the list are accepted: the page cannot make
    // the program read any other file.
    window_.bind(
        "s2a_open_recent",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 1, language_);
                const fs::path path = fs::u8path(stringArg(args, 0));
                bool known = false;
                {
                    const std::lock_guard<std::mutex> lock(userMutex_);
                    known = !path.empty() && user_.isRecentProject(path);
                }
                if (!known) {
                    throw UserError{tr(errorMessageKey(ErrorCode::FileNotFound), language)};
                }
                return openedJson(path, language);
            });
        });
    // Copy of the project being edited, kept until it is saved: (project, path where it was saved).
    window_.bind(
        "s2a_autosave",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const project::Project project = projectArg(args, 0, language_);
                const std::lock_guard<std::mutex> lock(userMutex_);
                if (const auto saved = user_.autosave(project, fs::u8path(stringArg(args, 1))); !saved.ok()) {
                    throw UserError{tr(errorMessageKey(saved.error().code), language_)};
                }
                return std::string("null");
            });
        });
    // The autosaved project, if any: {project, path, savedAt}.
    window_.bind(
        "s2a_autosaved",
        [this](const std::string& id, const std::string& /*request*/) {
            async(id, [this]() -> std::string {
                std::optional<userdata::Store::Autosaved> autosaved;
                {
                    const std::lock_guard<std::mutex> lock(userMutex_);
                    autosaved = user_.autosaved();
                }
                if (!autosaved) {
                    return "null";
                }
                // Saving goes back to the original file only when it is a .s2a one.
                const fs::path& original = autosaved->originalPath;
                const std::string path = original.extension() == ".s2a" ? original.u8string() : std::string{};
                return Json{{"project", Json::parse(project::toJson(autosaved->project))},
                            {"path", path},
                            {"savedAt", autosaved->savedAt}}
                    .dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });
    window_.bind(
        "s2a_autosave_clear",
        [this](const std::string& id, const std::string& /*request*/) {
            async(id, [this]() {
                const std::lock_guard<std::mutex> lock(userMutex_);
                user_.clearAutosave();
                return std::string("null");
            });
        });

    // "My blocks": groups of blocks saved by the user, shared by all their projects.
    const auto groupJson = [](const userdata::SavedBlocks& group) {
        return Json{{"id", group.id}, {"name", group.name}, {"blocks", Json::parse(project::toJson(group.blocks))}};
    };
    window_.bind(
        "s2a_library",
        [this, groupJson](const std::string& id, const std::string& /*request*/) {
            async(id, [this, groupJson]() {
                std::vector<userdata::SavedBlocks> groups;
                {
                    const std::lock_guard<std::mutex> lock(userMutex_);
                    groups = user_.library();
                }
                Json list = Json::array();
                for (const auto& group : groups) {
                    list.push_back(groupJson(group));
                }
                return list.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });
    // (name, blocks as a project) -> the saved group.
    window_.bind(
        "s2a_library_add",
        [this, groupJson](const std::string& id, const std::string& request) {
            async(id, [request, this, groupJson]() {
                const Json args = Json::parse(request);
                project::Project blocks = projectArg(args, 1, language_);
                const std::lock_guard<std::mutex> lock(userMutex_);
                auto added = user_.addToLibrary(stringArg(args, 0), std::move(blocks));
                if (!added.ok()) {
                    throw UserError{tr(errorMessageKey(added.error().code), language_)};
                }
                return groupJson(added.value()).dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });
    window_.bind(
        "s2a_library_rename",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const std::lock_guard<std::mutex> lock(userMutex_);
                if (auto renamed = user_.renameInLibrary(stringArg(args, 0), stringArg(args, 1)); !renamed.ok()) {
                    throw UserError{tr(errorMessageKey(renamed.error().code), language_)};
                }
                return std::string("null");
            });
        });
    window_.bind(
        "s2a_library_remove",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const std::lock_guard<std::mutex> lock(userMutex_);
                if (auto removed = user_.removeFromLibrary(stringArg(args, 0)); !removed.ok()) {
                    throw UserError{tr(errorMessageKey(removed.error().code), language_)};
                }
                return std::string("null");
            });
        });
    // Asks where to write the library; resolves to the file, or null when cancelled.
    window_.bind(
        "s2a_library_export",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() -> std::string {
                const Language language = languageArg(Json::parse(request), 0, language_);
                const fs::path suggestion = projectsDirectory() / (tr("webview.library_file", language) + ".s2ablocks");
                const fs::path file = chooseSaveFile(tr("webview.library_export_title", language), suggestion,
                                                     tr("webview.library_filter", language), ".s2ablocks", language);
                if (file.empty()) {
                    return "null";
                }
                const std::lock_guard<std::mutex> lock(userMutex_);
                if (auto exported = user_.exportLibrary(file); !exported.ok()) {
                    throw UserError{tr(errorMessageKey(exported.error().code), language)};
                }
                return Json(file.u8string()).dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });
    // Asks for a .s2ablocks file and adds its groups; resolves to how many, or null when cancelled.
    window_.bind(
        "s2a_library_import",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() -> std::string {
                const Language language = languageArg(Json::parse(request), 0, language_);
                const auto files = pfd::open_file(tr("webview.library_import_title", language), projectsDirectory().u8string(),
                                                  {tr("webview.library_filter", language), "*.s2ablocks"})
                                       .result();
                if (files.empty()) {
                    return "null";
                }
                const std::lock_guard<std::mutex> lock(userMutex_);
                auto imported = user_.importLibrary(fs::u8path(files.front()));
                if (!imported.ok()) {
                    throw UserError{tr(errorMessageKey(imported.error().code), language)};
                }
                return Json(imported.value()).dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    window_.bind(
        "s2a_ports",
        [this](const std::string& id, const std::string& /*request*/) {
            async(id, [this]() {
                Json ports = Json::array();
                const auto cli = arduinoCli(language_);
                const auto detected = cli.detectPorts();
                if (!detected.ok()) {
                    throw UserError{tr(errorMessageKey(detected.error().code), language_)};
                }
                for (const auto& port : detected.value()) {
                    if (port.protocol != "serial") {
                        continue;
                    }
                    const auto* board = BoardDatabase::builtin().find(port.boardId);
                    Json entry{{"address", port.address},
                               {"label", board != nullptr ? port.label + " - " + board->name : port.label}};
                    if (network::isWireless(port.address)) { // a board paired over Bluetooth (macOS)
                        // "/dev/cu.S2A-1A2B" -> "S2A-1A2B", "/dev/cu.HC-05" -> "HC-05"
                        std::string name = port.address.substr(port.address.rfind('/') + 1);
                        for (const std::string_view prefix : {"cu.", "tty."}) {
                            if (name.rfind(prefix, 0) == 0) {
                                name.erase(0, prefix.size());
                            }
                        }
                        entry["label"] = "Bluetooth - " + name;
                        entry["bluetooth"] = true;
                    }
                    if (!port.boardId.empty()) {
                        entry["boardId"] = port.boardId;
                    }
                    ports.push_back(std::move(entry));
                }
                return ports.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    // Library manager: the installed libraries, a search in the Arduino registry, install and
    // remove. The editor keeps the project's own list (Project::libraries).
    window_.bind(
        "s2a_libraries_installed",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Language language = languageArg(Json::parse(request), 0, language_);
                const auto cli = arduinoCli(language);
                const std::lock_guard<std::mutex> lock(librariesMutex_);
                const auto libraries = cli.listLibraries();
                if (!libraries.ok()) {
                    throw UserError{tr(errorMessageKey(libraries.error().code), language)};
                }
                Json list = Json::array();
                for (const auto& library : libraries.value()) {
                    list.push_back({{"name", library.name},
                                    {"version", library.version},
                                    {"author", library.author},
                                    {"sentence", library.sentence},
                                    {"website", library.website},
                                    {"includes", library.includes},
                                    {"builtIn", library.builtIn}});
                }
                return list.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });
    window_.bind(
        "s2a_libraries_search",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 1, language_);
                const auto cli = arduinoCli(language);
                const std::lock_guard<std::mutex> lock(librariesMutex_);
                // Without the internet, the list downloaded earlier still serves.
                std::string indexError;
                if (!libraryIndexReady_) {
                    const auto index = cli.updateLibraryIndex();
                    if (!index.ok()) {
                        indexError = tr(errorMessageKey(index.error().code), language);
                    } else if (const auto diagnosis = toolchain::diagnoseInstall(index.value());
                               diagnosis.problem != toolchain::Problem::None) {
                        indexError = toolchain::describe(diagnosis, language);
                    } else {
                        libraryIndexReady_ = true;
                    }
                }
                const auto found = cli.searchLibraries(stringArg(args, 0), 50);
                if (!found.ok()) {
                    throw UserError{indexError.empty() ? tr(errorMessageKey(found.error().code), language) : indexError};
                }
                Json list = Json::array();
                for (const auto& library : found.value()) {
                    list.push_back({{"name", library.name},
                                    {"author", library.author},
                                    {"sentence", library.sentence},
                                    {"category", library.category},
                                    {"website", library.website},
                                    {"latestVersion", library.latestVersion},
                                    {"versions", library.versions},
                                    {"architectures", library.architectures}});
                }
                return list.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });
    const auto libraryChange = [this](const std::string& id, const std::string& request, bool install) {
        async(id, [request, install, this]() {
            const Json args = Json::parse(request);
            const Language language = languageArg(args, install ? 2 : 1, language_);
            const std::string name = stringArg(args, 0);
            const auto cli = arduinoCli(language);
            const std::lock_guard<std::mutex> lock(librariesMutex_);
            const auto outcome = install ? cli.installLibrary(name, stringArg(args, 1)) : cli.uninstallLibrary(name);
            if (!outcome.ok()) {
                throw UserError{tr(errorMessageKey(outcome.error().code), language)};
            }
            if (const auto diagnosis = toolchain::diagnoseInstall(outcome.value());
                diagnosis.problem != toolchain::Problem::None) {
                return Json{{"ok", false},
                            {"message", toolchain::describe(diagnosis, language)},
                            {"details", diagnosis.technical}}
                    .dump(-1, ' ', false, Json::error_handler_t::replace);
            }
            return Json{{"ok", outcome.value().success},
                        {"message", outcome.value().success ? std::string{} : outcome.value().error},
                        {"details", outcome.value().out + outcome.value().err}}
                .dump(-1, ' ', false, Json::error_handler_t::replace);
        });
    };
    window_.bind(
        "s2a_library_install",
        [libraryChange](const std::string& id, const std::string& request) {
            libraryChange(id, request, true);
        });
    window_.bind(
        "s2a_library_uninstall",
        [libraryChange](const std::string& id, const std::string& request) {
            libraryChange(id, request, false);
        });

    // Optional board packages (ESP32, ESP8266...): listed with their size, installed and removed
    // from the settings or when a board that needs one is chosen.
    window_.bind(
        "s2a_board_packages",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Language language = languageArg(Json::parse(request), 0, language_);
                const auto cli = arduinoCli(language);
                const auto states = toolchain::packageStates(cli);
                if (!states.ok()) {
                    throw UserError{tr(errorMessageKey(states.error().code), language)};
                }
                Json list = Json::array();
                for (const auto& state : states.value()) {
                    const auto& package = *state.package;
                    list.push_back({{"id", package.id},
                                    {"name", package.name(language)},
                                    {"boards", package.boards},
                                    {"shipped", package.shipped},
                                    {"version", package.version},
                                    {"installedVersion", state.installedVersion},
                                    {"downloadBytes", state.downloadBytes},
                                    {"diskBytes", state.diskBytes}});
                }
                return list.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });
    const auto packageChange = [this](const std::string& id, const std::string& request, bool install) {
        async(id, [request, install, this]() {
            const Json args = Json::parse(request);
            const Language language = languageArg(args, 1, language_);
            const auto* package = toolchain::BoardPackages::builtin().find(stringArg(args, 0));
            if (package == nullptr) {
                throw UserError{tr("error.unknown_board", language)};
            }
            const auto cli = arduinoCli(language);
            const std::lock_guard<std::mutex> lock(packagesMutex_);
            const auto outcome =
                install ? toolchain::installBoardPackage(cli, *package) : toolchain::removeBoardPackage(cli, *package);
            if (!outcome.ok()) {
                throw UserError{tr(errorMessageKey(outcome.error().code), language)};
            }
            if (const auto diagnosis = toolchain::diagnoseInstall(outcome.value());
                diagnosis.problem != toolchain::Problem::None) {
                return Json{{"ok", false},
                            {"message", toolchain::describe(diagnosis, language)},
                            {"details", diagnosis.technical}}
                    .dump(-1, ' ', false, Json::error_handler_t::replace);
            }
            return Json{{"ok", outcome.value().success},
                        {"message", outcome.value().success ? std::string{} : outcome.value().error},
                        {"details", outcome.value().out + outcome.value().err}}
                .dump(-1, ' ', false, Json::error_handler_t::replace);
        });
    };
    window_.bind(
        "s2a_board_package_install",
        [packageChange](const std::string& id, const std::string& request) {
            packageChange(id, request, true);
        });
    window_.bind(
        "s2a_board_package_remove",
        [packageChange](const std::string& id, const std::string& request) {
            packageChange(id, request, false);
        });

    const auto build = [this](const std::string& id, const std::string& request, bool upload) {
        async(id, [request, upload, this]() {
            const Json args = Json::parse(request);
            const Language language = languageArg(args, upload ? 2 : 1, language_);
            const project::Project project = projectArg(args, 0, language);
            toolchain::BuildOptions options;
            if (upload) {
                options.port = stringArg(args, 1);
                if (network::isWireless(options.port)) {
                    throw UserError{tr("live.wifi_upload", language)};
                }
                if (sim::isSimulated(options.port)) {
                    throw UserError{tr("live.sim_upload", language)};
                }
                closeMonitor(); // the editor opens it again after the upload
                waitForLiveClosed(); // a Live session just stopped may still hold the port
            }
            const auto cli = arduinoCli(language);
            return outcomeJson(toolchain::buildProject(cli, project, language, options));
        });
    };
    // Live mode: start (async: the session object is created at once, connection runs on its
    // own thread), update the blocks, stop, and read the state (polled by the editor).
    bindPortCall("s2a_live_start", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const Language language = languageArg(args, 2, language_);
        std::string message;
        try {
            project::Project project = projectArg(args, 0, language);
            const Board* board = BoardDatabase::builtin().find(project.boardId);
            const std::string port = stringArg(args, 1);
            if (board == nullptr || port.empty()) {
                return Json{{"ok", false}, {"message", tr("editor.no_port", language)}}.dump(-1, ' ', false, Json::error_handler_t::replace);
            }
            closeMonitor(); // Live mode needs the port; its output is shown instead
            const std::lock_guard<std::mutex> lock(liveMutex_);
            retireLive(); // stopping takes a moment (outputs switched off): not on the UI thread
            // The new session connects on its own thread: it waits there until the old one has
            // released the port, so the old "switch everything off" cannot reach the new program.
            const auto afterClosing = [closing = liveClosing_](live::Session::Opener open) -> live::Session::Opener {
                return [closing, open = std::move(open)](const std::string& target) {
                    if (closing.valid()) {
                        closing.wait();
                    }
                    return open(target);
                };
            };
            if (sim::isSimulated(port)) {
                // Simulation: a new virtual board, which the editor draws and whose inputs it changes.
                simBoard_ = std::make_shared<sim::VirtualBoard>(*board);
                live_ = std::make_unique<live::Session>(
                    std::move(project), *board, port, language,
                    [simulated = simBoard_](const std::string& /*port*/)
                        -> Result<std::unique_ptr<serial::LineTransport>> {
                        return std::unique_ptr<serial::LineTransport>(std::make_unique<sim::Connection>(simulated));
                    },
                    live::Session::Flasher{});
                return Json{{"ok", true}}.dump(-1, ' ', false, Json::error_handler_t::replace);
            }
            // Over Wi-Fi or Bluetooth the firmware cannot be uploaded: the board must already run it.
            // The 4th argument: the user agreed to replace the board's program with the Live firmware.
            const bool mayUploadFirmware = args.size() > 3 && args[3].is_boolean() && args[3].get<bool>();
            live_ = std::make_unique<live::Session>(
                std::move(project), *board, port, language, afterClosing(live::Session::opener()),
                network::isWireless(port) ? live::Session::Flasher{} : live::Session::arduinoCliFlasher(*board, port),
                mayUploadFirmware);
            return Json{{"ok", true}}.dump(-1, ' ', false, Json::error_handler_t::replace);
        } catch (const UserError& error) {
            message = error.message;
        }
        return Json{{"ok", false}, {"message", message}}.dump(-1, ' ', false, Json::error_handler_t::replace);
    });
    bindSync("s2a_live_update", [this](const std::string& request) {
        const Json args = Json::parse(request);
        try {
            project::Project project = projectArg(args, 0, language_);
            const std::lock_guard<std::mutex> lock(liveMutex_);
            if (live_) {
                live_->update(std::move(project));
            }
        } catch (const UserError& error) {
            // Half-built blocks: the board keeps the previous ones, and the editor says why (as a
            // hint, not an error: the child is still editing, audit #179).
            return Json{{"ok", false}, {"message", error.message}}.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        return Json{{"ok", true}}.dump(-1, ' ', false, Json::error_handler_t::replace);
    });
    bindPortCall("s2a_live_stop", [this](const std::string& /*request*/) {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        if (live_) {
            live_->stop();
            retireLive();
        }
        return std::string{};
    });
    // Step-by-step mode: pause (true) or run freely (false), and "Next block" while paused.
    bindSync("s2a_live_pause", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const bool paused = !args.empty() && args[0].is_boolean() && args[0].get<bool>();
        const std::lock_guard<std::mutex> lock(liveMutex_);
        if (live_) {
            live_->pause(paused);
        }
        return std::string{};
    });
    bindSync("s2a_live_step", [this](const std::string& /*request*/) {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        if (live_) {
            live_->stepOnce();
        }
        return std::string{};
    });
    bindSync("s2a_live_state", [this](const std::string& /*request*/) {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        if (!live_) {
            return Json{{"state", "stopped"}}.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        const auto snapshot = live_->snapshot();
        return Json{{"state", stateName(snapshot.state)},
                    {"message", snapshot.message},
                    {"details", snapshot.details},
                    {"running", snapshot.runningBlocks},
                    {"paused", snapshot.paused},
                    {"wiring", wiringHints(snapshot.wiring)},
                    {"problems", snapshot.problemBlocks},
                    {"variables", snapshot.variables},
                    {"readings", snapshot.readings},
                    {"output", outputLines(snapshot.output)},
                    {"outputCount", snapshot.outputCount},
                    {"line", snapshot.errorLine},
                    {"file", snapshot.errorFile}}
            .dump(-1, ' ', false, Json::error_handler_t::replace);
    });

    // Simulation: what the virtual board shows (polled by the editor), and the inputs the user
    // changes on it: ["press", pin, true], ["analog", pin, 0-1023], ["distance", trig, cm],
    // ["climate", pin, celsius, percent], ["phone", text].
    bindSync("s2a_sim_state", [this](const std::string& /*request*/) {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        if (!simBoard_) {
            return Json(nullptr).dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        return simulationJson(simBoard_->state());
    });
    bindSync("s2a_sim_input", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const std::lock_guard<std::mutex> lock(liveMutex_);
        if (!simBoard_ || args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
            return std::string{};
        }
        const std::string kind = args[0].get<std::string>();
        const std::string pin = args[1].get<std::string>();
        const auto number = [&args](std::size_t index) {
            return args.size() > index && args[index].is_number() ? args[index].get<double>() : 0.0;
        };
        if (kind == "press" && args.size() > 2 && args[2].is_boolean()) {
            simBoard_->press(pin, args[2].get<bool>());
        } else if (kind == "analog") {
            simBoard_->setAnalog(pin, static_cast<int>(number(2)));
        } else if (kind == "distance") {
            simBoard_->setDistance(pin, number(2));
        } else if (kind == "climate") {
            simBoard_->setClimate(pin, number(2), number(3));
        } else if (kind == "phone") {
            simBoard_->phoneSays(pin); // the text of the phone
        }
        return std::string{};
    });

    // Serial monitor (outside Live mode): open, send a line, close, and read (polled).
    bindPortCall("s2a_monitor_open", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const Language language = languageArg(args, 2, language_);
        const std::string port = stringArg(args, 0);
        const int baud = args.size() > 1 && args[1].is_number_integer() ? args[1].get<int>() : codegen::kSerialBaud;
        if (port.empty()) {
            return Json{{"ok", false}, {"message", tr("editor.no_port", language)}}.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        if (network::isWireless(port)) {
            return Json{{"ok", false}, {"message", tr("live.wifi_monitor", language)}}.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        if (sim::isSimulated(port)) {
            return Json{{"ok", false}, {"message", tr("live.sim_monitor", language)}}.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        {
            // A Live session (maybe still connecting) owns the port: its output is shown instead.
            const std::lock_guard<std::mutex> lock(liveMutex_);
            if (live_) {
                return Json{{"ok", false}, {"message", tr(errorMessageKey(ErrorCode::PortFailed), language)}}.dump(-1, ' ', false, Json::error_handler_t::replace);
            }
        }
        closeMonitor();
        waitForLiveClosed(); // a Live session just stopped may still hold the port (on the port thread)
        auto opened = serial::Monitor::open(port, baud);
        if (!opened.ok()) {
            return Json{{"ok", false},
                        {"message", tr(errorMessageKey(opened.error().code), language)},
                        {"details", opened.error().detail}}
                .dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        const std::lock_guard<std::mutex> lock(monitorMutex_);
        monitor_ = std::move(opened).value();
        return Json{{"ok", true}}.dump(-1, ' ', false, Json::error_handler_t::replace);
    });
    bindSync("s2a_monitor_send", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const std::lock_guard<std::mutex> lock(monitorMutex_);
        if (monitor_) {
            monitor_->send(stringArg(args, 0));
        }
        return std::string{};
    });
    bindPortCall("s2a_monitor_close", [this](const std::string& /*request*/) {
        closeMonitor();
        return std::string{};
    });
    bindSync("s2a_monitor_state", [this](const std::string& /*request*/) {
        const std::lock_guard<std::mutex> lock(monitorMutex_);
        if (!monitor_) {
            return Json{{"open", false}}.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        const auto snapshot = monitor_->snapshot();
        Json state{{"open", snapshot.open}, {"lines", snapshot.lines}, {"count", snapshot.count}};
        if (snapshot.error) {
            state["message"] = tr(errorMessageKey(snapshot.error->code), language_);
            state["details"] = snapshot.error->detail;
        }
        return state.dump(-1, ' ', false, Json::error_handler_t::replace);
    });

    // Wi-Fi of an ESP32, over its USB cable: saves the home network on the board (the Live
    // firmware is uploaded first if needed), then waits until it joined it.
    window_.bind(
        "s2a_wifi_setup",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 4, language_);
                const std::string port = stringArg(args, 0);
                const std::string name = stringArg(args, 1);
                const std::string password = stringArg(args, 2);
                const Board* board = BoardDatabase::builtin().find(stringArg(args, 3));
                if (board == nullptr || port.empty() || network::isWireless(port)) {
                    throw UserError{tr("live.wifi_upload", language)};
                }
                const auto failure = [&](const Error& error) {
                    return UserError{tr(errorMessageKey(error.code), language)};
                };
                const auto connection = openLiveOverUsb(*board, port, language);
                live::LiveLink link(*connection);
                if (const auto joined = link.joinNetwork(name, password); !joined.ok()) {
                    throw failure(joined.error());
                }
                // Joining takes a few seconds; a wrong password never succeeds.
                live::LiveLink::NetworkState state;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(name.empty() ? 0 : 20);
                do {
                    auto current = link.networkState();
                    if (!current.ok()) {
                        throw failure(current.error());
                    }
                    state = std::move(current).value();
                    if (!state.address.empty() || name.empty()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                } while (std::chrono::steady_clock::now() < deadline);
                const std::string key = name.empty() ? "live.wifi_forgotten"
                                        : state.address.empty() ? "live.wifi_not_joined"
                                                                : "live.wifi_joined";
                Json result{{"ok", name.empty() || !state.address.empty()},
                            {"accessPoint", state.accessPoint},
                            {"accessPointAddress", state.accessPointAddress},
                            {"address", state.address},
                            {"message", tr(key, language, {{"network", name}, {"address", state.address},
                                                           {"access_point", state.accessPoint}})}};
                return result.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    // HC-05 / HC-06 module of an Arduino board, over its USB cable: saves its pins and speed on
    // the board (the Live firmware is uploaded first if needed); null forgets it.
    window_.bind(
        "s2a_bluetooth_module",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 3, language_);
                const std::string port = stringArg(args, 0);
                const Board* board = BoardDatabase::builtin().find(stringArg(args, 1));
                if (board == nullptr || port.empty() || network::isWireless(port)) {
                    throw UserError{tr("module.usb", language)};
                }
                std::optional<live::BluetoothModule> wanted;
                if (args.size() > 2 && args[2].is_object()) {
                    // Pins by the label printed on the board ("10", "A8"), as the editor shows them.
                    const auto pinNumber = [&](const char* key, int fallback) {
                        const Json& value = args[2].contains(key) ? args[2].at(key) : Json();
                        if (value.is_number_integer()) {
                            return value.get<int>();
                        }
                        const Pin* pin = value.is_string() ? board->findPin(value.get<std::string>()) : nullptr;
                        return pin != nullptr ? pin->number : (value.is_null() ? fallback : -1);
                    };
                    live::BluetoothModule module;
                    module.rx = pinNumber("rx", module.rx);
                    module.tx = pinNumber("tx", module.tx);
                    module.baud = args[2].value("baud", module.baud);
                    if (module.rx < 0 || module.tx < 0) {
                        throw UserError{tr("module.pin_missing", language)};
                    }
                    if (const std::string problem = live::bluetoothModuleProblem(*board, module); !problem.empty()) {
                        throw UserError{tr("module." + problem, language)};
                    }
                    wanted = module;
                }
                const auto connection = openLiveOverUsb(*board, port, language);
                live::LiveLink link(*connection);
                const auto failure = [&](const Error& error) {
                    return UserError{tr(errorMessageKey(error.code), language)};
                };
                if (const auto saved = link.setBluetoothModule(wanted); !saved.ok()) {
                    throw failure(saved.error());
                }
                const auto running = link.bluetoothModule();
                if (!running.ok()) {
                    throw failure(running.error());
                }
                const auto label = [board](int number) {
                    const Pin* pin = board->findPin(number);
                    return pin != nullptr ? pin->label : std::to_string(number);
                };
                std::string message;
                if (!wanted) {
                    message = tr("module.forgotten", language);
                } else {
                    const std::string key = running.value() == wanted ? "module.saved" : "module.saved_restart";
                    message = tr(key, language, {{"rx", label(wanted->rx)}, {"tx", label(wanted->tx)},
                                                 {"baud", std::to_string(wanted->baud)}});
                }
                return Json{{"ok", true}, {"message", message}}.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    // Bluetooth boards around the computer (a search takes about 8 s).
    window_.bind(
        "s2a_bluetooth_search",
        [this](const std::string& id, const std::string& request) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 0, language_);
                if (!network::bluetoothByAddress()) {
                    return Json{{"ok", true}, {"byAddress", false}, {"devices", Json::array()}}.dump(-1, ' ', false, Json::error_handler_t::replace);
                }
                const auto found = network::findBluetoothBoards(std::chrono::seconds(8));
                if (!found.ok()) {
                    return Json{{"ok", false},
                                {"message", tr("live.bluetooth_off", language)},
                                {"details", found.error().detail}}
                        .dump(-1, ' ', false, Json::error_handler_t::replace);
                }
                Json devices = Json::array();
                for (const auto& device : found.value()) {
                    devices.push_back({{"name", device.name}, {"address", device.address}});
                }
                return Json{{"ok", true}, {"byAddress", true}, {"devices", devices}}.dump(-1, ' ', false, Json::error_handler_t::replace);
            });
        });

    window_.bind(
        "s2a_build",
        [build](const std::string& id, const std::string& request) { build(id, request, false); });
    window_.bind(
        "s2a_upload",
        [build](const std::string& id, const std::string& request) { build(id, request, true); });
}

fs::path locateFrontend() {
    std::error_code error;
    std::vector<fs::path> candidates;
    if (const fs::path configured = process::environmentPath("S2A_FRONTEND_DIR"); !configured.empty()) {
        candidates.push_back(configured);
    }
    if (const fs::path directory = process::executableDirectory(); !directory.empty()) {
        candidates.push_back(directory / "frontend");                                        // build tree, Windows
        candidates.push_back(directory.parent_path() / "Resources" / "frontend");            // macOS bundle
        candidates.push_back(directory.parent_path() / "share" / "scratch2arduino" / "frontend"); // Linux install
    }
    for (const fs::path& candidate : candidates) {
        if (fs::is_regular_file(candidate / "index.html", error)) {
            return candidate;
        }
    }
    return {};
}

std::string fileUrl(const fs::path& file) {
    std::string path = fs::absolute(file).generic_u8string();
    if (path.empty() || path.front() != '/') {
        path.insert(0, "/"); // C:/... -> /C:/...
    }
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string url = "file://";
    for (const unsigned char c : path) {
        if (std::isalnum(c) != 0 || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
            url += static_cast<char>(c);
        } else {
            url += '%';
            url += kHex[c >> 4U];
            url += kHex[c & 0x0FU];
        }
    }
    return url;
}

} // namespace s2a::webview
