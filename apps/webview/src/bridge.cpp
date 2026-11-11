// Core headers first: webview.h (via bridge.hpp) brings X11 macros on Linux.
#include "s2a/arduino_cli.hpp"
#include "s2a/boards.hpp"
#include "s2a/build.hpp"
#include "s2a/codegen.hpp"
#include "s2a/convert.hpp"
#include "s2a/live.hpp"
#include "s2a/network.hpp"
#include "s2a/process.hpp"
#include "s2a/project.hpp"
#include "s2a/usercode.hpp"
#include "s2a/version.hpp"
#include "s2a/wiring.hpp"

#include "bridge.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include <portable-file-dialogs.h>
#include <webview/webview.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h, which it needs.
#include <shellapi.h>
#endif

namespace s2a::webview {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr int kResolve = 0;
constexpr int kReject = 1;

/// Thrown by a call to reject its promise with a message for the user.
struct UserError {
    std::string message;
};

Language languageArg(const Json& args, std::size_t index, Language fallback) {
    return index < args.size() && args[index].is_string() ? languageFromLocale(args[index].get<std::string>())
                                                         : fallback;
}

project::Project projectArg(const Json& args, std::size_t index, Language language) {
    if (index >= args.size()) {
        throw UserError{tr(errorMessageKey(ErrorCode::InvalidJson), language)};
    }
    auto parsed = project::fromJson(args[index].dump());
    if (!parsed.ok()) {
        throw UserError{tr(errorMessageKey(parsed.error().code), language) + " (" + parsed.error().detail + ")"};
    }
    return std::move(parsed).value();
}

/// Bytes of a base64 text (a "data:...;base64," prefix is skipped); invalid characters are ignored.
std::string base64Decode(std::string_view text) {
    if (const auto comma = text.find(','); text.substr(0, 5) == "data:" && comma != std::string_view::npos) {
        text.remove_prefix(comma + 1);
    }
    static constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string bytes;
    unsigned int buffer = 0;
    int bits = 0;
    for (const char c : text) {
        const auto value = kAlphabet.find(c);
        if (value == std::string_view::npos) {
            continue; // padding, line breaks
        }
        buffer = (buffer << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return bytes;
}

std::string stringArg(const Json& args, std::size_t index) {
    return index < args.size() && args[index].is_string() ? args[index].get<std::string>() : std::string{};
}

fs::path environmentPath(const char* name) {
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): nothing calls setenv
    return value != nullptr ? fs::u8path(value) : fs::path{};
}

/// Documents/Scratch2Arduino, created on first use: where dialogs open by default.
fs::path projectsDirectory() {
#ifdef _WIN32
    const fs::path home = environmentPath("USERPROFILE");
#else
    const fs::path home = environmentPath("HOME");
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
    static const std::string kRepository = "https://github.com/Zargith/Scratch2Arduino/";
    if (url.rfind(kRepository, 0) != 0 || url.size() > 512) {
        return false;
    }
    for (const char c : url) {
        const bool safe = std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                          std::string_view("/:.-_~%?=&#+").find(c) != std::string_view::npos;
        if (!safe) {
            return false;
        }
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
        .dump();
}

} // namespace

Bridge::Bridge(::webview::webview& window, Language language)
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

Bridge::~Bridge() {
    closeMonitor();
    {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        live_.reset(); // switches the outputs off
    }
    std::list<Worker> workers;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        workers.swap(workers_);
    }
    for (Worker& worker : workers) {
        worker.thread.join();
    }
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
            window_.resolve(id, kResolve, task());
        } catch (const UserError& error) {
            window_.resolve(id, kReject, Json(error.message).dump());
        } catch (const std::exception& error) {
            window_.resolve(id, kReject, Json(std::string("internal error: ") + error.what()).dump());
        }
        done->store(true);
    });
    workers_.push_back({std::move(thread), std::move(done)});
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
        .dump();
}

void Bridge::bind() {
    window_.bind("s2a_hello", [this](const std::string& /*request*/) {
        return Json{{"language", std::string(languageCode(language_))}, {"version", std::string(version::kString)}}.dump();
    });

    window_.bind("s2a_set_title", [this](const std::string& request) {
        window_.set_title(stringArg(Json::parse(request), 0));
        return std::string{};
    });

    window_.bind(
        "s2a_generate",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const project::Project project = projectArg(args, 0, language_);
                return Json(codegen::generate(project).source).dump();
            });
        },
        nullptr);

    // Functions and variables of a "My code" file, for its palette category: (language, code).
    // Quick and never failing: called each time the user goes back to the blocks.
    // Rewrites a program in another text language: (code, from, to) -> {code, toCheck: [lines]}.
    // Never fails: what it does not understand comes back as comments, their lines in toCheck.
    window_.bind("s2a_convert", [](const std::string& request) {
        const Json args = Json::parse(request);
        const auto from = project::programLanguageFromId(stringArg(args, 1));
        const auto to = project::programLanguageFromId(stringArg(args, 2));
        const std::string code = stringArg(args, 0);
        if (!from || !to) {
            return Json{{"code", code}, {"toCheck", Json::array()}}.dump();
        }
        const auto converted = convert::convert(code, *from, *to);
        return Json{{"code", converted.code}, {"toCheck", converted.toCheck}}.dump();
    });

    window_.bind("s2a_code_declarations", [](const std::string& request) {
        const Json args = Json::parse(request);
        const auto language = project::programLanguageFromId(stringArg(args, 0));
        Json list = Json::array();
        if (!language) {
            return list.dump();
        }
        for (const auto& declaration : usercode::declarations(*language, stringArg(args, 1))) {
            list.push_back({{"kind", declaration.kind == usercode::Declaration::Kind::Function ? "function" : "variable"},
                            {"name", declaration.name},
                            {"parameters", declaration.parameters},
                            {"returnsValue", declaration.returnsValue},
                            {"readOnly", declaration.readOnly},
                            {"line", declaration.line}});
        }
        return list.dump();
    });

    // Wiring of the project (components, connections, warnings), from its blocks.
    window_.bind(
        "s2a_wiring",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
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
                                               {"via", connection.via}});
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
                return result.dump();
            });
        },
        nullptr);

    // A picture made by the editor (the wiring diagram as PNG), saved where the user chooses.
    window_.bind(
        "s2a_save_image",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this]() -> std::string {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 2, language_);
                const std::string name = stringArg(args, 0);
                const std::string data = base64Decode(stringArg(args, 1));
                const fs::path suggestion =
                    projectsDirectory() / (codegen::identifier(name.empty() ? "schema" : name) + ".png");
                const std::string chosen = pfd::save_file(tr("webview.image_title", language), suggestion.u8string(),
                                                          {tr("webview.image_filter", language), "*.png"},
                                                          pfd::opt::force_overwrite)
                                               .result();
                if (chosen.empty()) {
                    return "null";
                }
                fs::path file = fs::u8path(chosen);
                if (file.extension() != ".png") {
                    file += ".png";
                }
                std::ofstream out(file, std::ios::binary);
                out << data;
                if (!out) {
                    throw UserError{tr(errorMessageKey(ErrorCode::WriteFailed), language)};
                }
                return Json(file.u8string()).dump();
            });
        },
        nullptr);

    window_.bind(
        "s2a_open",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
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
        },
        nullptr);
    // Project given on the command line, handed to the editor once (null afterwards).
    window_.bind(
        "s2a_startup_project",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            fs::path path;
            std::swap(path, startupFile_);
            async(id, [request, path, this]() -> std::string {
                if (path.empty()) {
                    return "null";
                }
                return openedJson(path, languageArg(Json::parse(request), 0, language_));
            });
        },
        nullptr);

    window_.bind(
        "s2a_save",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this]() -> std::string {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 2, language_);
                const project::Project project = projectArg(args, 0, language);
                fs::path path = fs::u8path(stringArg(args, 1));
                if (path.empty()) {
                    const fs::path suggestion =
                        projectsDirectory() / (codegen::identifier(project.name.empty() ? "projet" : project.name) + ".s2a");
                    const std::string chosen = pfd::save_file(tr("webview.save_title", language), suggestion.u8string(),
                                                              {tr("webview.file_filter", language), "*.s2a"},
                                                              pfd::opt::force_overwrite)
                                                   .result();
                    if (chosen.empty()) {
                        return "null";
                    }
                    path = fs::u8path(chosen);
                    if (path.extension() != ".s2a") {
                        path += ".s2a";
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
                return Json(path.u8string()).dump();
            });
        },
        nullptr);

    // Settings of the editor: {language ("fr", "en" or "" for the system's), checkUpdates}.
    window_.bind("s2a_settings", [this](const std::string& /*request*/) {
        userdata::Settings settings;
        {
            const std::lock_guard<std::mutex> lock(userMutex_);
            settings = user_.settings();
        }
        return Json{{"language", settings.language}, {"checkUpdates", settings.checkUpdates}, {"theme", settings.theme}}
            .dump();
    });
    window_.bind("s2a_save_settings", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const Json given = args.is_array() && !args.empty() && args[0].is_object() ? args[0] : Json::object();
        userdata::Settings settings;
        settings.language = given.value("language", std::string{});
        settings.checkUpdates = given.value("checkUpdates", true);
        settings.theme = given.value("theme", std::string{});
        const std::lock_guard<std::mutex> lock(userMutex_);
        return Json(user_.saveSettings(settings).ok()).dump();
    });
    // Opens a page of the project's GitHub repository (new release) in the browser.
    window_.bind(
        "s2a_open_url",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request]() { return Json(openProjectPage(stringArg(Json::parse(request), 0))).dump(); });
        },
        nullptr);

    // Recent projects, newest first: [{path, name}].
    window_.bind(
        "s2a_recent",
        [this](const std::string& id, const std::string& /*request*/, void* /*arg*/) {
            async(id, [this]() {
                std::vector<fs::path> paths;
                {
                    const std::lock_guard<std::mutex> lock(userMutex_);
                    paths = user_.recentProjects();
                }
                Json list = Json::array();
                for (const fs::path& path : paths) {
                    list.push_back({{"path", path.u8string()}, {"name", path.stem().u8string()}});
                }
                return list.dump();
            });
        },
        nullptr);
    // Reopens a recent project. Only paths of the list are accepted: the page cannot make
    // the program read any other file.
    window_.bind(
        "s2a_open_recent",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
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
        },
        nullptr);
    // Copy of the project being edited, kept until it is saved: (project, path where it was saved).
    window_.bind(
        "s2a_autosave",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const project::Project project = projectArg(args, 0, language_);
                const std::lock_guard<std::mutex> lock(userMutex_);
                if (const auto saved = user_.autosave(project, fs::u8path(stringArg(args, 1))); !saved.ok()) {
                    throw UserError{tr(errorMessageKey(saved.error().code), language_)};
                }
                return std::string("null");
            });
        },
        nullptr);
    // The autosaved project, if any: {project, path, savedAt}.
    window_.bind(
        "s2a_autosaved",
        [this](const std::string& id, const std::string& /*request*/, void* /*arg*/) {
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
                    .dump();
            });
        },
        nullptr);
    window_.bind(
        "s2a_autosave_clear",
        [this](const std::string& id, const std::string& /*request*/, void* /*arg*/) {
            async(id, [this]() {
                const std::lock_guard<std::mutex> lock(userMutex_);
                user_.clearAutosave();
                return std::string("null");
            });
        },
        nullptr);

    // "My blocks": groups of blocks saved by the user, shared by all their projects.
    const auto groupJson = [](const userdata::SavedBlocks& group) {
        return Json{{"id", group.id}, {"name", group.name}, {"blocks", Json::parse(project::toJson(group.blocks))}};
    };
    window_.bind(
        "s2a_library",
        [this, groupJson](const std::string& id, const std::string& /*request*/, void* /*arg*/) {
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
                return list.dump();
            });
        },
        nullptr);
    // (name, blocks as a project) -> the saved group.
    window_.bind(
        "s2a_library_add",
        [this, groupJson](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this, groupJson]() {
                const Json args = Json::parse(request);
                project::Project blocks = projectArg(args, 1, language_);
                const std::lock_guard<std::mutex> lock(userMutex_);
                auto added = user_.addToLibrary(stringArg(args, 0), std::move(blocks));
                if (!added.ok()) {
                    throw UserError{tr(errorMessageKey(added.error().code), language_)};
                }
                return groupJson(added.value()).dump();
            });
        },
        nullptr);
    window_.bind(
        "s2a_library_rename",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const std::lock_guard<std::mutex> lock(userMutex_);
                if (auto renamed = user_.renameInLibrary(stringArg(args, 0), stringArg(args, 1)); !renamed.ok()) {
                    throw UserError{tr(errorMessageKey(renamed.error().code), language_)};
                }
                return std::string("null");
            });
        },
        nullptr);
    window_.bind(
        "s2a_library_remove",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const std::lock_guard<std::mutex> lock(userMutex_);
                if (auto removed = user_.removeFromLibrary(stringArg(args, 0)); !removed.ok()) {
                    throw UserError{tr(errorMessageKey(removed.error().code), language_)};
                }
                return std::string("null");
            });
        },
        nullptr);
    // Asks where to write the library; resolves to the file, or null when cancelled.
    window_.bind(
        "s2a_library_export",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this]() -> std::string {
                const Language language = languageArg(Json::parse(request), 0, language_);
                const fs::path suggestion = projectsDirectory() / (tr("webview.library_file", language) + ".s2ablocks");
                const std::string chosen = pfd::save_file(tr("webview.library_export_title", language), suggestion.u8string(),
                                                          {tr("webview.library_filter", language), "*.s2ablocks"},
                                                          pfd::opt::force_overwrite)
                                               .result();
                if (chosen.empty()) {
                    return "null";
                }
                fs::path file = fs::u8path(chosen);
                if (file.extension() != ".s2ablocks") {
                    file += ".s2ablocks";
                }
                const std::lock_guard<std::mutex> lock(userMutex_);
                if (auto exported = user_.exportLibrary(file); !exported.ok()) {
                    throw UserError{tr(errorMessageKey(exported.error().code), language)};
                }
                return Json(file.u8string()).dump();
            });
        },
        nullptr);
    // Asks for a .s2ablocks file and adds its groups; resolves to how many, or null when cancelled.
    window_.bind(
        "s2a_library_import",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
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
                return Json(imported.value()).dump();
            });
        },
        nullptr);

    window_.bind(
        "s2a_ports",
        [this](const std::string& id, const std::string& /*request*/, void* /*arg*/) {
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
                        entry["label"] = "Bluetooth - " + port.address.substr(port.address.find(network::kBoardNamePrefix));
                        entry["bluetooth"] = true;
                    }
                    if (!port.boardId.empty()) {
                        entry["boardId"] = port.boardId;
                    }
                    ports.push_back(std::move(entry));
                }
                return ports.dump();
            });
        },
        nullptr);

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
                closeMonitor(); // the editor opens it again after the upload
            }
            const auto cli = arduinoCli(language);
            return outcomeJson(toolchain::buildProject(cli, project, language, options));
        });
    };
    // Live mode: start (async: the session object is created at once, connection runs on its
    // own thread), update the blocks, stop, and read the state (polled by the editor).
    window_.bind("s2a_live_start", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const Language language = languageArg(args, 2, language_);
        std::string message;
        try {
            project::Project project = projectArg(args, 0, language);
            const Board* board = BoardDatabase::builtin().find(project.boardId);
            const std::string port = stringArg(args, 1);
            if (board == nullptr || port.empty()) {
                return Json{{"ok", false}, {"message", tr("editor.no_port", language)}}.dump();
            }
            closeMonitor(); // Live mode needs the port; its output is shown instead
            const std::lock_guard<std::mutex> lock(liveMutex_);
            if (live_) {
                // Stopping can take a moment (outputs switched off): not on the UI thread.
                std::thread([old = std::move(live_)]() mutable { old.reset(); }).detach();
            }
            // Over Wi-Fi or Bluetooth the firmware cannot be uploaded: the board must already run it.
            live_ = std::make_unique<live::Session>(
                std::move(project), *board, port, language, live::Session::opener(),
                network::isWireless(port) ? live::Session::Flasher{} : live::Session::arduinoCliFlasher(*board, port));
            return Json{{"ok", true}}.dump();
        } catch (const UserError& error) {
            message = error.message;
        }
        return Json{{"ok", false}, {"message", message}}.dump();
    });
    window_.bind("s2a_live_update", [this](const std::string& request) {
        const Json args = Json::parse(request);
        try {
            project::Project project = projectArg(args, 0, language_);
            const std::lock_guard<std::mutex> lock(liveMutex_);
            if (live_) {
                live_->update(std::move(project));
            }
        } catch (const UserError&) {
            // Half-built blocks are simply ignored until they make a valid project.
        }
        return std::string{};
    });
    window_.bind("s2a_live_stop", [this](const std::string& /*request*/) {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        if (live_) {
            live_->stop();
            std::thread([old = std::move(live_)]() mutable { old.reset(); }).detach();
        }
        return std::string{};
    });
    window_.bind("s2a_live_state", [this](const std::string& /*request*/) {
        const std::lock_guard<std::mutex> lock(liveMutex_);
        if (!live_) {
            return Json{{"state", "stopped"}}.dump();
        }
        const auto snapshot = live_->snapshot();
        return Json{{"state", stateName(snapshot.state)},
                    {"message", snapshot.message},
                    {"details", snapshot.details},
                    {"running", snapshot.runningBlocks},
                    {"problems", snapshot.problemBlocks},
                    {"variables", snapshot.variables},
                    {"readings", snapshot.readings},
                    {"output", outputLines(snapshot.output)},
                    {"outputCount", snapshot.outputCount},
                    {"line", snapshot.errorLine},
                    {"file", snapshot.errorFile}}
            .dump();
    });

    // Serial monitor (outside Live mode): open, send a line, close, and read (polled).
    window_.bind("s2a_monitor_open", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const Language language = languageArg(args, 2, language_);
        const std::string port = stringArg(args, 0);
        const int baud = args.size() > 1 && args[1].is_number_integer() ? args[1].get<int>() : codegen::kSerialBaud;
        if (port.empty()) {
            return Json{{"ok", false}, {"message", tr("editor.no_port", language)}}.dump();
        }
        if (network::isWireless(port)) {
            return Json{{"ok", false}, {"message", tr("live.wifi_monitor", language)}}.dump();
        }
        closeMonitor();
        auto opened = serial::Monitor::open(port, baud);
        if (!opened.ok()) {
            return Json{{"ok", false},
                        {"message", tr(errorMessageKey(opened.error().code), language)},
                        {"details", opened.error().detail}}
                .dump();
        }
        const std::lock_guard<std::mutex> lock(monitorMutex_);
        monitor_ = std::move(opened).value();
        return Json{{"ok", true}}.dump();
    });
    window_.bind("s2a_monitor_send", [this](const std::string& request) {
        const Json args = Json::parse(request);
        const std::lock_guard<std::mutex> lock(monitorMutex_);
        if (monitor_) {
            monitor_->send(stringArg(args, 0));
        }
        return std::string{};
    });
    window_.bind("s2a_monitor_close", [this](const std::string& /*request*/) {
        closeMonitor();
        return std::string{};
    });
    window_.bind("s2a_monitor_state", [this](const std::string& /*request*/) {
        const std::lock_guard<std::mutex> lock(monitorMutex_);
        if (!monitor_) {
            return Json{{"open", false}}.dump();
        }
        const auto snapshot = monitor_->snapshot();
        Json state{{"open", snapshot.open}, {"lines", snapshot.lines}, {"count", snapshot.count}};
        if (snapshot.error) {
            state["message"] = tr(errorMessageKey(snapshot.error->code), language_);
            state["details"] = snapshot.error->detail;
        }
        return state.dump();
    });

    // Wi-Fi of an ESP32, over its USB cable: saves the home network on the board (the Live
    // firmware is uploaded first if needed), then waits until it joined it.
    window_.bind(
        "s2a_wifi_setup",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
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
                closeMonitor();
                {
                    const std::lock_guard<std::mutex> lock(liveMutex_);
                    live_.reset(); // the port is needed
                }
                const auto failure = [&](const Error& error) {
                    return UserError{tr(errorMessageKey(error.code), language)};
                };
                auto opened = serial::SerialPort::open(port, live::kBaud);
                if (!opened.ok()) {
                    throw failure(opened.error());
                }
                std::unique_ptr<serial::SerialPort> connection = std::move(opened).value();
                if (!live::LiveLink(*connection).handshake(std::chrono::milliseconds(3500)).ok()) {
                    connection.reset(); // no Live firmware yet: install it (with Wi-Fi), then connect again
                    std::string details;
                    if (const auto flashed = live::Session::arduinoCliFlasher(*board, port)(details); !flashed.ok()) {
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
                }
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
                return result.dump();
            });
        },
        nullptr);

    // Bluetooth boards around the computer (a search takes about 8 s).
    window_.bind(
        "s2a_bluetooth_search",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this]() {
                const Json args = Json::parse(request);
                const Language language = languageArg(args, 0, language_);
                if (!network::bluetoothByAddress()) {
                    return Json{{"ok", true}, {"byAddress", false}, {"devices", Json::array()}}.dump();
                }
                const auto found = network::findBluetoothBoards(std::chrono::seconds(8));
                if (!found.ok()) {
                    return Json{{"ok", false},
                                {"message", tr("live.bluetooth_off", language)},
                                {"details", found.error().detail}}
                        .dump();
                }
                Json devices = Json::array();
                for (const auto& device : found.value()) {
                    devices.push_back({{"name", device.name}, {"address", device.address}});
                }
                return Json{{"ok", true}, {"byAddress", true}, {"devices", devices}}.dump();
            });
        },
        nullptr);

    window_.bind(
        "s2a_build",
        [build](const std::string& id, const std::string& request, void* /*arg*/) { build(id, request, false); },
        nullptr);
    window_.bind(
        "s2a_upload",
        [build](const std::string& id, const std::string& request, void* /*arg*/) { build(id, request, true); },
        nullptr);
}

fs::path locateFrontend() {
    std::error_code error;
    std::vector<fs::path> candidates;
    if (const fs::path configured = environmentPath("S2A_FRONTEND_DIR"); !configured.empty()) {
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
