#include "s2a/build.hpp"

#include "s2a/blocks.hpp"
#include "s2a/codegen.hpp"
#include "s2a/diagnostics.hpp"
#include "s2a/validator.hpp"

#include <algorithm>
#include <fstream>
#include <map>

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

} // namespace

BuildResult buildProject(const ArduinoCli& cli, const project::Project& project, Language language,
                         const BuildOptions& options) {
    const Board* board = BoardDatabase::builtin().find(project.boardId);
    if (board == nullptr) {
        return failure(tr("build.unknown_board", language, {{"board", project.boardId}}));
    }

    // Mistakes in the blocks are reported here, never while the user edits.
    const auto issues = validation::validate(project, BlockCatalog::builtin(), board, language);
    std::vector<std::string> warnings;
    std::vector<std::string> warningBlockIds;
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

    // Generate the sketch. The folder and the .ino must have the same name.
    const std::string name = codegen::identifier(project.name.empty() ? "projet" : project.name);
    const fs::path sketch = options.workDirectory / "sketches" / name;
    const auto generated = codegen::generate(project, BlockCatalog::builtin(), board);
    std::error_code error;
    fs::create_directories(sketch, error);
    {
        std::ofstream file(sketch / (name + ".ino"), std::ios::binary | std::ios::trunc);
        file << generated.source;
        if (!file) {
            return failure(tr(errorMessageKey(ErrorCode::WriteFailed), language), sketch.u8string());
        }
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

    notify(options, BuildStep::Compiling);
    const fs::path build = options.workDirectory / "build" / (name + "-" + board->id);
    const auto compiled = cli.compile(sketch, *board, build);
    if (!compiled.ok()) {
        return failure(tr(errorMessageKey(compiled.error().code), language), compiled.error().detail);
    }
    if (const Diagnosis diagnosis = diagnoseCompile(compiled.value()); diagnosis.problem != Problem::None) {
        BuildResult result = explain(diagnosis, language);
        result.blockIds = blocksAt(diagnosis.messages, generated.blockLines, name + ".ino");
        result.sketch = sketch;
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
    for (const MemoryUsage& section : compiled.value().memory) {
        params.emplace_back(section.section + "_percent",
                            section.max > 0 ? std::to_string(section.size * 100 / section.max) : "?");
    }
    if (options.port.empty()) {
        result.ok = true;
        result.message = tr("build.compiled", language, params);
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
