#include "bridge.hpp"

#include "s2a/arduino_cli.hpp"
#include "s2a/build.hpp"
#include "s2a/codegen.hpp"
#include "s2a/process.hpp"
#include "s2a/project.hpp"
#include "s2a/version.hpp"

#include <cctype>
#include <cstdlib>
#include <exception>
#include <nlohmann/json.hpp>
#include <portable-file-dialogs.h>
#include <webview/webview.h>

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

std::string stringArg(const Json& args, std::size_t index) {
    return index < args.size() && args[index].is_string() ? args[index].get<std::string>() : std::string{};
}

fs::path environmentPath(const char* name) {
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): read before threads start
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

toolchain::ArduinoCli arduinoCli(Language language) {
    const fs::path executable = toolchain::locateArduinoCli();
    if (executable.empty()) {
        throw UserError{tr("cli.arduino_cli_missing", language)};
    }
    return toolchain::ArduinoCli(executable, toolchain::Directories::defaults());
}

std::string outcomeJson(const toolchain::BuildResult& result) {
    return Json{{"ok", result.ok}, {"message", result.message}, {"details", result.details}, {"blockIds", result.blockIds}}
        .dump();
}

} // namespace

Bridge::Bridge(::webview::webview& window, Language language) : window_(window), language_(language) {}

Bridge::~Bridge() {
    std::vector<std::thread> workers;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        workers.swap(workers_);
    }
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

template <typename Work> void Bridge::async(const std::string& id, Work work) {
    const std::lock_guard<std::mutex> lock(mutex_);
    workers_.emplace_back([this, id, task = std::move(work)]() {
        try {
            window_.resolve(id, kResolve, task());
        } catch (const UserError& error) {
            window_.resolve(id, kReject, Json(error.message).dump());
        } catch (const std::exception& error) {
            window_.resolve(id, kReject, Json(std::string("internal error: ") + error.what()).dump());
        }
    });
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

    window_.bind(
        "s2a_open",
        [this](const std::string& id, const std::string& request, void* /*arg*/) {
            async(id, [request, this]() -> std::string {
                const Language language = languageArg(Json::parse(request), 0, language_);
                const auto files = pfd::open_file(tr("webview.open_title", language), projectsDirectory().u8string(),
                                                  {tr("webview.file_filter", language), "*.s2a"})
                                       .result();
                if (files.empty()) {
                    return "null";
                }
                const fs::path path = fs::u8path(files.front());
                auto loaded = project::load(path);
                if (!loaded.ok()) {
                    throw UserError{tr(errorMessageKey(loaded.error().code), language)};
                }
                return Json{{"path", path.u8string()}, {"project", Json::parse(project::toJson(loaded.value()))}}.dump();
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
                return Json(path.u8string()).dump();
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
                               {"label", board != nullptr ? port.label + " — " + board->name : port.label}};
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
            }
            const auto cli = arduinoCli(language);
            return outcomeJson(toolchain::buildProject(cli, project, language, options));
        });
    };
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
