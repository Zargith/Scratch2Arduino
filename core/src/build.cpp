#include "s2a/build.hpp"

#include "s2a/blocks.hpp"
#include "s2a/codegen.hpp"
#include "s2a/diagnostics.hpp"
#include "s2a/script.hpp"
#include "s2a/usercode.hpp"
#include "s2a/validator.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>

namespace s2a::toolchain {

namespace {

namespace fs = std::filesystem;

void notify(const BuildOptions& options, BuildStep step) {
    if (options.progress) {
        options.progress(step);
    }
}

BuildResult failure(std::string message, std::string details = {}) {
    BuildResult result;
    result.message = std::move(message);
    result.details = std::move(details);
    return result;
}

BuildResult explain(const Diagnosis& diagnosis, Language language) {
    std::string details = diagnosis.technical;
    for (const CompilerMessage& message : diagnosis.messages) {
        details += "\n" + std::to_string(message.line) + ": " + message.text;
    }
    return failure(describe(diagnosis, language), std::move(details));
}

/// Block whose code starts at, or closest before, each compiler error line of the sketch.
std::vector<std::string> blocksAt(const std::vector<CompilerMessage>& messages,
                                  const std::map<std::string, int>& blockLines, const std::string& inoName) {
    std::vector<std::string> ids;
    for (const CompilerMessage& message : messages) {
        if (fs::u8path(message.file).filename().u8string() != inoName || message.line <= 0) {
            continue;
        }
        const std::string* best = nullptr;
        int bestLine = 0;
        for (const auto& [id, line] : blockLines) {
            if (line <= message.line && line > bestLine) {
                best = &id;
                bestLine = line;
            }
        }
        if (best != nullptr && std::find(ids.begin(), ids.end(), *best) == ids.end()) {
            ids.push_back(*best);
        }
    }
    return ids;
}

/// The errors of the compiler in the user's sketch, by line, explained when possible.
std::vector<CodeProblem> problemsAt(const std::vector<CompilerMessage>& messages, const std::string& fileName,
                                    const std::string& file, Language language) {
    std::vector<CodeProblem> problems;
    for (const CompilerMessage& message : messages) {
        // Errors inside a library are reported in the status bar only (no line of the user's code).
        if (fs::u8path(message.file).filename().u8string() != fileName) {
            continue;
        }
        problems.push_back({message.line, message.column, explainCompilerMessage(message, language), file});
    }
    return problems;
}

/// True when `code` defines function `name` ("void setup(" with any spacing).
bool definesFunction(const std::string& code, const std::string& name) {
    const std::regex pattern("\\bvoid\\s+" + name + "\\s*\\(");
    return std::regex_search(code, pattern);
}

/// Python and JavaScript run on the computer (Live mode): Check only reads them.
BuildResult checkScript(const project::Project& project, Language language, bool upload) {
    if (upload) {
        return failure(tr("text.script_upload", language));
    }
    if (const auto error = script::check(project.language, project.code, language)) {
        BuildResult result = failure(script::describe(*error, language), error->details);
        result.problems.push_back({error->line, 0, error->message, ""});
        return result;
    }
    BuildResult result;
    result.ok = true;
    result.message = tr("text.script_ok", language);
    return result;
}

} // namespace

BuildResult buildProject(const ArduinoCli& cli, const project::Project& project, Language language,
                         const BuildOptions& options) {
    const Board* board = BoardDatabase::builtin().find(project.boardId);
    if (board == nullptr) {
        return failure(tr("build.unknown_board", language, {{"board", project.boardId}}));
    }

    std::vector<std::string> warnings;
    std::vector<std::string> warningBlockIds;
    std::string source;
    std::string userSource; ///< "My code" in C++, compiled with the blocks.
    std::map<std::string, int> blockLines;
    const bool text = project.language != project::ProgramLanguage::Blocks;
    if (project.language == project::ProgramLanguage::Python ||
        project.language == project::ProgramLanguage::JavaScript) {
        return checkScript(project, language, !options.port.empty());
    }
    if (project.language == project::ProgramLanguage::Cpp) {
        // The sketch is compiled as typed; only what the compiler would say badly is checked first.
        if (project.code.find_first_not_of(" \t\r\n") == std::string::npos) {
            return failure(tr("text.cpp_empty", language));
        }
        for (const char* function : {"setup", "loop"}) {
            if (!definesFunction(project.code, function)) {
                return failure(tr("text.cpp_missing_function", language, {{"name", function}}));
            }
        }
        source = project.code;
    } else {
        // Python and JavaScript code runs on the computer: blocks using it need the Live mode.
        if (project.userCode && script::isScript(project.userCode->language) && usercode::usesUserCode(project)) {
            return failure(tr("text.usercode_live_only", language,
                              {{"language", project.userCode->language == project::ProgramLanguage::Python ? "Python"
                                                                                                          : "JavaScript"}}));
        }
        if (project.userCode && project.userCode->language == project::ProgramLanguage::Cpp) {
            userSource = project.userCode->code;
        }
        // Mistakes in the blocks are reported here, never while the user edits.
        const auto issues = validation::validate(project, BlockCatalog::builtin(), board, language);
        for (const validation::Issue& issue : issues) {
            if (issue.severity == validation::Severity::Warning) {
                warnings.push_back(validation::describe(issue, language));
                warningBlockIds.push_back(issue.blockId);
            }
        }
        if (validation::hasErrors(issues)) {
            BuildResult result;
            std::size_t errors = 0;
            for (const validation::Issue& issue : issues) {
                if (issue.severity == validation::Severity::Error) {
                    if (errors++ == 0) {
                        result.message = validation::describe(issue, language);
                    }
                    if (!issue.blockId.empty()) {
                        result.blockIds.push_back(issue.blockId);
                    }
                }
            }
            result.details = tr("validation.blocked", language, {{"count", std::to_string(errors)}});
            for (const validation::Issue& issue : issues) {
                result.details += "\n- " + validation::describe(issue, language);
            }
            result.warnings = std::move(warnings);
            result.warningBlockIds = std::move(warningBlockIds);
            return result;
        }
        auto generated = codegen::generate(project, BlockCatalog::builtin(), board);
        source = std::move(generated.source);
        blockLines = std::move(generated.blockLines);
    }

    // Write the sketch. The folder and the .ino must have the same name.
    const std::string name = codegen::identifier(project.name.empty() ? "projet" : project.name);
    const fs::path sketch = options.workDirectory / "sketches" / name;
    std::error_code error;
    fs::create_directories(sketch, error);
    {
        std::ofstream file(sketch / (name + ".ino"), std::ios::binary | std::ios::trunc);
        file << source;
        if (!file) {
            return failure(tr(errorMessageKey(ErrorCode::WriteFailed), language), sketch.u8string());
        }
    }
    const fs::path userFile = sketch / std::string(codegen::kUserCodeFile);
    if (project.userCode && project.userCode->language == project::ProgramLanguage::Cpp && !text) {
        std::ofstream file(userFile, std::ios::binary | std::ios::trunc);
        file << userSource;
        if (!userSource.empty() && userSource.back() != '\n') {
            file << '\n';
        }
        if (!file) {
            return failure(tr(errorMessageKey(ErrorCode::WriteFailed), language), userFile.u8string());
        }
    } else {
        fs::remove(userFile, error); // left by an earlier build of the same project
    }

    // Board tools, downloaded once per board family.
    const auto installed = cli.isPlatformInstalled(board->platform);
    if (!installed.ok()) {
        return failure(tr(errorMessageKey(installed.error().code), language), installed.error().detail);
    }
    if (!installed.value()) {
        notify(options, BuildStep::InstallingTools);
        const auto outcome = cli.installPlatform(board->platform);
        if (!outcome.ok()) {
            return failure(tr(errorMessageKey(outcome.error().code), language), outcome.error().detail);
        }
        if (const Diagnosis diagnosis = diagnoseInstall(outcome.value()); diagnosis.problem != Problem::None) {
            return explain(diagnosis, language);
        }
    }

    // Component libraries (Servo...), downloaded the first time a block needs them.
    const auto missing = missingLibraries(cli, requiredLibraries(source + "\n" + userSource));
    if (!missing.ok()) {
        return failure(tr(errorMessageKey(missing.error().code), language), missing.error().detail);
    }
    if (!missing.value().empty()) {
        notify(options, BuildStep::InstallingLibraries);
        const auto outcome = cli.installLibraries(missing.value());
        if (!outcome.ok()) {
            return failure(tr(errorMessageKey(outcome.error().code), language), outcome.error().detail);
        }
        if (const Diagnosis diagnosis = diagnoseInstall(outcome.value()); diagnosis.problem != Problem::None) {
            return explain(diagnosis, language);
        }
    }

    notify(options, BuildStep::Compiling);
    const fs::path build = options.workDirectory / "build" / (name + "-" + board->id);
    const auto compiled = cli.compile(sketch, *board, build);
    if (!compiled.ok()) {
        return failure(tr(errorMessageKey(compiled.error().code), language), compiled.error().detail);
    }
    if (const Diagnosis diagnosis = diagnoseCompile(compiled.value()); diagnosis.problem != Problem::None) {
        BuildResult result = explain(diagnosis, language);
        result.sketch = sketch;
        if (text && diagnosis.problem == Problem::LibraryMissing) {
            // Typed code can include any library: say which ones are installed by themselves.
            result.message = tr("text.cpp_library_missing", language, diagnosis.params);
        }
        if (!text) {
            result.blockIds = blocksAt(diagnosis.messages, blockLines, name + ".ino");
            result.problems = problemsAt(diagnosis.messages, std::string(codegen::kUserCodeFile), "user", language);
            if (diagnosis.problem == Problem::LibraryMissing && !userSource.empty()) {
                result.message = tr("text.cpp_library_missing", language, diagnosis.params);
            }
            if (diagnosis.problem == Problem::CompilerError && !result.problems.empty()) {
                const CodeProblem& first = result.problems.front();
                result.message = tr("text.usercode_errors", language,
                                    {{"count", std::to_string(result.problems.size())},
                                     {"line", std::to_string(first.line)},
                                     {"message", first.message}});
            }
            return result;
        }
        result.problems = problemsAt(diagnosis.messages, name + ".ino", "", language);
        if (diagnosis.problem == Problem::CompilerError && !result.problems.empty()) {
            const CodeProblem& first = result.problems.front();
            result.message = tr("text.cpp_errors", language,
                                {{"count", std::to_string(result.problems.size())},
                                 {"line", std::to_string(first.line)},
                                 {"message", first.message}});
        }
        return result;
    }

    BuildResult result;
    result.sketch = sketch;
    result.details = compiled.value().out;
    result.warnings = warnings;
    result.warningBlockIds = warningBlockIds;
    for (const std::string& warning : warnings) {
        result.details = "- " + warning + "\n" + result.details;
    }
    TranslationParams params{{"board", board->name}};
    bool sized = false; // the ESP8266 tools do not report the size of the program
    for (const MemoryUsage& section : compiled.value().memory) {
        params.emplace_back(section.section + "_percent",
                            section.max > 0 ? std::to_string(section.size * 100 / section.max) : "?");
        sized = sized || section.section == "text";
    }
    if (options.port.empty()) {
        result.ok = true;
        result.message = tr(sized ? "build.compiled" : "build.compiled_no_size", language, params);
        return result;
    }

    notify(options, BuildStep::Uploading);
    const auto uploaded = cli.upload(sketch, *board, build, options.port);
    if (!uploaded.ok()) {
        return failure(tr(errorMessageKey(uploaded.error().code), language), uploaded.error().detail);
    }
    if (const Diagnosis diagnosis = diagnoseUpload(uploaded.value(), options.port);
        diagnosis.problem != Problem::None) {
        BuildResult failed = explain(diagnosis, language);
        failed.sketch = sketch;
        return failed;
    }
    result.ok = true;
    result.message = tr("build.uploaded", language, params);
    result.details += uploaded.value().out;
    return result;
}

} // namespace s2a::toolchain
