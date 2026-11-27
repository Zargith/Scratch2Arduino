#include "s2a/project.hpp"

#include "project_format.hpp"
#include "s2a/version.hpp"

#include <algorithm>
#include <array>
#include "json_input.hpp"

#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>

namespace s2a::project {

namespace {

using Json = nlohmann::json;
using Files = std::map<std::string, std::string>;

/// Where the code of a text project or of "My code" goes.
enum class CodeStorage {
    Inline,    ///< In project.json (bare project.json, editor bridge, examples): versions 1 and 2.
    InArchive, ///< In its own file beside project.json in a .s2a archive: version 3.
};

/// Thrown while reading project.json; carries the JSON path of the problem.
class FormatError : public std::runtime_error {
public:
    FormatError(const std::string& path, const std::string& what) : std::runtime_error(path + ": " + what) {}
};

// --- Writing -------------------------------------------------------------------------

Json blockToJson(const Block& block);

Json stackToJson(const Stack& stack) {
    Json array = Json::array();
    for (const Block& block : stack) {
        array.push_back(blockToJson(block));
    }
    return array;
}

Json blockToJson(const Block& block) {
    Json json{{"id", block.id}, {"type", block.type}};
    if (!block.fields.empty()) {
        json["fields"] = block.fields;
    }
    if (!block.inputs.empty()) {
        Json inputs = Json::object();
        for (const auto& [name, input] : block.inputs) {
            Json value{{"value", input.value}};
            if (input.block) {
                value["block"] = blockToJson(*input.block);
            }
            inputs[name] = std::move(value);
        }
        json["inputs"] = std::move(inputs);
    }
    if (!block.statements.empty()) {
        Json statements = Json::object();
        for (const auto& [name, stack] : block.statements) {
            statements[name] = stackToJson(stack);
        }
        json["statements"] = std::move(statements);
    }
    return json;
}

// --- Reading -------------------------------------------------------------------------

const Json& member(const Json& object, const char* key, const std::string& path) {
    if (!object.contains(key)) {
        throw FormatError(path, std::string("missing '") + key + "'");
    }
    return object.at(key);
}

std::string stringAt(const Json& object, const char* key, const std::string& path) {
    const Json& value = member(object, key, path);
    if (!value.is_string()) {
        throw FormatError(path + "." + key, "must be a string");
    }
    return value.get<std::string>();
}

double numberAt(const Json& object, const char* key, const std::string& path, double fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    const Json& value = object.at(key);
    if (!value.is_number()) {
        throw FormatError(path + "." + key, "must be a number");
    }
    return value.get<double>();
}

const Json& objectAt(const Json& object, const char* key, const std::string& path) {
    static const Json empty = Json::object();
    if (!object.contains(key)) {
        return empty;
    }
    const Json& value = object.at(key);
    if (!value.is_object()) {
        throw FormatError(path + "." + key, "must be an object");
    }
    return value;
}

const Json& arrayAt(const Json& object, const char* key, const std::string& path) {
    static const Json empty = Json::array();
    if (!object.contains(key)) {
        return empty;
    }
    const Json& value = object.at(key);
    if (!value.is_array()) {
        throw FormatError(path + "." + key, "must be an array");
    }
    return value;
}

/// Text of a code file named in project.json, looked up in the archive entries.
std::string fileContent(const std::string& name, const std::string& path, const Files* files) {
    if (files == nullptr) {
        throw FormatError(path, "the code file '" + name + "' can only be read from a .s2a archive");
    }
    const auto found = files->find(name);
    if (found == files->end()) {
        throw FormatError(path, "the code file '" + name + "' is missing from the archive");
    }
    return found->second;
}

Block blockFromJson(const Json& json, const std::string& path, int depth);

Stack stackFromJson(const Json& array, const std::string& path, int depth) {
    Stack stack;
    for (std::size_t index = 0; index < array.size(); ++index) {
        stack.push_back(blockFromJson(array.at(index), path + "[" + std::to_string(index) + "]", depth));
    }
    return stack;
}

Block blockFromJson(const Json& json, const std::string& path, int depth) {
    // Guards the recursion against malicious files; real projects are far shallower.
    constexpr int kMaxDepth = 256;
    if (depth > kMaxDepth) {
        throw FormatError(path, "blocks are nested too deeply");
    }
    if (!json.is_object()) {
        throw FormatError(path, "a block must be an object");
    }
    Block block;
    block.id = stringAt(json, "id", path);
    block.type = stringAt(json, "type", path);
    for (const auto& [name, value] : objectAt(json, "fields", path).items()) {
        if (!value.is_string()) {
            throw FormatError(path + ".fields." + name, "must be a string");
        }
        block.fields[name] = value.get<std::string>();
    }
    for (const auto& [name, value] : objectAt(json, "inputs", path).items()) {
        const std::string inputPath = path + ".inputs." + name;
        if (!value.is_object()) {
            throw FormatError(inputPath, "must be an object");
        }
        Input input;
        if (value.contains("value")) {
            input.value = stringAt(value, "value", inputPath);
        }
        if (value.contains("block")) {
            input.block = blockFromJson(value.at("block"), inputPath + ".block", depth + 1);
        }
        block.inputs[name] = std::move(input);
    }
    for (const auto& [name, value] : objectAt(json, "statements", path).items()) {
        const std::string stackPath = path + ".statements." + name;
        if (!value.is_array()) {
            throw FormatError(stackPath, "must be an array");
        }
        block.statements[name] = stackFromJson(value, stackPath, depth + 1);
    }
    return block;
}

/// Upgrades an older project.json in place, one version at a time.
void migrate(Json& json, int fromVersion) {
    // Version 2 added "language" and "code" (programs written as text): version 1 projects are
    // block projects. When the format changes again, add a step here and bump kFormatVersion.
    if (fromVersion < 2) {
        json["language"] = std::string(programLanguageId(ProgramLanguage::Blocks));
        json.erase("code");
        json.erase("userCode");
    }
}

Project projectFromJson(Json json, const Files* files) {
    // Named root path: passing a temporary to the reference-returning helpers trips
    // GCC's -Wdangling-reference false positive.
    const std::string root = "$";
    if (!json.is_object()) {
        throw FormatError("$", "project.json must be an object");
    }
    if (!json.contains("format") || json.at("format") != std::string(kFormatName)) {
        throw FormatError("$.format", "not a Scratch2Arduino project");
    }
    const Json& version = member(json, "formatVersion", root);
    if (!version.is_number_integer() || version.get<long long>() < 1 || version.get<long long>() > kFormatVersion) {
        throw FormatError("$.formatVersion", "must be a positive integer");
    }
    migrate(json, version.get<int>());

    Project project;
    project.name = json.contains("name") ? stringAt(json, "name", root) : std::string{};
    project.boardId = stringAt(json, "board", root);

    const Json& variables = arrayAt(json, "variables", root);
    for (std::size_t index = 0; index < variables.size(); ++index) {
        const std::string path = "$.variables[" + std::to_string(index) + "]";
        const Json& node = variables.at(index);
        if (!node.is_object()) {
            throw FormatError(path, "a variable must be an object");
        }
        Variable variable{stringAt(node, "id", path), stringAt(node, "name", path), numberAt(node, "initialValue", path, 0)};
        // A second variable with the same id (a hand-edited file) would become a second
        // declaration of the same global: the first one is kept (audit #189).
        if (std::none_of(project.variables.begin(), project.variables.end(),
                         [&variable](const Variable& kept) { return kept.id == variable.id; })) {
            project.variables.push_back(std::move(variable));
        }
    }

    if (json.contains("functions")) { // optional: projects without functions do not have it
        const Json& functions = arrayAt(json, "functions", root);
        for (std::size_t index = 0; index < functions.size(); ++index) {
            const std::string path = "$.functions[" + std::to_string(index) + "]";
            const Json& node = functions.at(index);
            if (!node.is_object()) {
                throw FormatError(path, "a function must be an object");
            }
            Function function{stringAt(node, "id", path), stringAt(node, "name", path), {}};
            if (node.contains("parameters")) { // optional: left out for a function without inputs
                const Json& parameters = arrayAt(node, "parameters", path);
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    if (!parameters.at(i).is_string()) {
                        throw FormatError(path + ".parameters[" + std::to_string(i) + "]", "a parameter must be a name");
                    }
                    function.parameters.push_back(parameters.at(i).get<std::string>());
                }
            }
            if (std::none_of(project.functions.begin(), project.functions.end(),
                             [&function](const Function& kept) { return kept.id == function.id; })) {
                project.functions.push_back(std::move(function));
            }
        }
    }

    if (json.contains("language")) {
        const std::string id = stringAt(json, "language", root);
        const auto language = programLanguageFromId(id);
        if (!language) {
            throw FormatError("$.language", "unknown language '" + id + "'");
        }
        project.language = *language;
    }
    if (json.contains("code") && project.language != ProgramLanguage::Blocks) {
        const Json& code = json.at("code");
        if (code.is_object()) { // version 3: {"file": "program.py"}
            project.code = fileContent(stringAt(code, "file", "$.code"), "$.code.file", files);
        } else {
            project.code = stringAt(json, "code", root);
        }
    }
    if (json.contains("userCode")) {
        const Json& node = json.at("userCode");
        if (!node.is_object()) {
            throw FormatError("$.userCode", "must be an object");
        }
        const std::string id = stringAt(node, "language", "$.userCode");
        const auto language = programLanguageFromId(id);
        if (!language || *language == ProgramLanguage::Blocks) {
            throw FormatError("$.userCode.language", "unknown language '" + id + "'");
        }
        project.userCode = UserCode{*language, node.contains("file") // version 3: in its own file
                                                   ? fileContent(stringAt(node, "file", "$.userCode"),
                                                                 "$.userCode.file", files)
                                                   : stringAt(node, "code", "$.userCode")};
    }

    if (json.contains("libraries")) { // optional, like "functions"
        const Json& libraries = arrayAt(json, "libraries", root);
        for (std::size_t index = 0; index < libraries.size(); ++index) {
            const std::string path = "$.libraries[" + std::to_string(index) + "]";
            const Json& node = libraries.at(index);
            if (!node.is_object()) {
                throw FormatError(path, "a library must be an object");
            }
            project.libraries.push_back(
                {stringAt(node, "name", path), node.contains("version") ? stringAt(node, "version", path) : std::string{}});
        }
    }

    if (json.contains("savedWith")) {
        project.savedWith = stringAt(json, "savedWith", root);
    }

    const Json& scripts = arrayAt(json, "scripts", root);
    for (std::size_t index = 0; index < scripts.size(); ++index) {
        const std::string path = "$.scripts[" + std::to_string(index) + "]";
        const Json& node = scripts.at(index);
        if (!node.is_object()) {
            throw FormatError(path, "a script must be an object");
        }
        Script script;
        script.x = numberAt(node, "x", path, 0);
        script.y = numberAt(node, "y", path, 0);
        script.blocks = stackFromJson(arrayAt(node, "blocks", path), path + ".blocks", 0);
        project.scripts.push_back(std::move(script));
    }
    return project;
}

} // namespace

std::string_view programLanguageId(ProgramLanguage language) noexcept {
    switch (language) {
    case ProgramLanguage::Blocks:
        return "blocks";
    case ProgramLanguage::Cpp:
        return "cpp";
    case ProgramLanguage::Python:
        return "python";
    case ProgramLanguage::JavaScript:
        return "javascript";
    }
    return "blocks";
}

std::optional<ProgramLanguage> programLanguageFromId(std::string_view id) noexcept {
    for (const ProgramLanguage language :
         {ProgramLanguage::Blocks, ProgramLanguage::Cpp, ProgramLanguage::Python, ProgramLanguage::JavaScript}) {
        if (programLanguageId(language) == id) {
            return language;
        }
    }
    return std::nullopt;
}

std::string newId() {
    static constexpr std::string_view kAlphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, kAlphabet.size() - 1);
    std::string id(20, ' ');
    for (char& c : id) {
        c = kAlphabet[pick(generator)];
    }
    return id;
}

Project newProject(Language language) {
    Project project;
    project.name = tr("project.default_name", language);
    Script script;
    script.x = 40;
    script.y = 40;
    script.blocks.push_back(Block{newId(), "events_when_program_starts", {}, {}, {}});
    project.scripts.push_back(std::move(script));
    return project;
}

namespace {

/// project.json as a JSON document. With CodeStorage::InArchive, the code goes to `files`.
Json projectDocument(const Project& project, CodeStorage storage, Files* files) {
    Json variables = Json::array();
    for (const Variable& variable : project.variables) {
        variables.push_back({{"id", variable.id}, {"name", variable.name}, {"initialValue", variable.initialValue}});
    }
    Json scripts = Json::array();
    for (const Script& script : project.scripts) {
        scripts.push_back({{"x", script.x}, {"y", script.y}, {"blocks", stackToJson(script.blocks)}});
    }
    const bool text = project.language != ProgramLanguage::Blocks;
    const bool userCode = project.userCode.has_value();
    Json json{
        {"format", std::string(kFormatName)},
        {"formatVersion", !text && !userCode ? 1 : storage == CodeStorage::Inline ? 2 : 3}, // see kFormatVersion
        {"name", project.name},
        {"board", project.boardId},
        {"variables", std::move(variables)},
        {"scripts", std::move(scripts)},
    };
    if (!project.functions.empty()) { // left out otherwise: older projects keep the same bytes
        Json functions = Json::array();
        for (const Function& function : project.functions) {
            Json node = {{"id", function.id}, {"name", function.name}};
            if (!function.parameters.empty()) {
                node["parameters"] = function.parameters;
            }
            functions.push_back(std::move(node));
        }
        json["functions"] = std::move(functions);
    }
    if (!project.libraries.empty()) { // left out otherwise, as "functions"
        Json libraries = Json::array();
        for (const Library& library : project.libraries) {
            Json node{{"name", library.name}};
            if (!library.version.empty()) {
                node["version"] = library.version;
            }
            libraries.push_back(std::move(node));
        }
        json["libraries"] = std::move(libraries);
    }
    const bool inArchive = storage == CodeStorage::InArchive;
    if (text) { // left out for blocks: block projects keep the bytes of version 1
        json["language"] = std::string(programLanguageId(project.language));
        if (inArchive) {
            const std::string name = codeFileName(CodeFile::Program, project.language);
            (*files)[name] = project.code;
            json["code"] = {{"file", name}};
        } else {
            json["code"] = project.code;
        }
    }
    if (userCode) { // older versions would drop it silently: hence version 2 (or 3)
        Json node{{"language", std::string(programLanguageId(project.userCode->language))}};
        if (inArchive) {
            const std::string name = codeFileName(CodeFile::MyCode, project.userCode->language);
            (*files)[name] = project.userCode->code;
            node["file"] = name;
        } else {
            node["code"] = project.userCode->code;
        }
        json["userCode"] = std::move(node);
    }
    if (inArchive) { // ignored by older versions: block projects stay readable by them
        json["savedWith"] = std::string(version::kString);
    }
    return json;
}

/// Parses project.json; `files` are the archive entries (nullptr for a bare project.json).
Result<Project> parse(std::string_view text, const Files* files) {
    Json json = s2a::detail::parseUntrustedJson(text);
    if (json.is_discarded()) {
        return Error{ErrorCode::InvalidJson, "project.json is not valid JSON"};
    }
    if (json.is_object() && json.contains("formatVersion") && json.at("formatVersion").is_number_integer() &&
        json.at("formatVersion").get<long long>() > kFormatVersion) {
        return Error{ErrorCode::UnsupportedVersion,
                     "project format " + json.at("formatVersion").dump() + " is newer than " +
                         std::to_string(kFormatVersion)};
    }
    try {
        return projectFromJson(std::move(json), files);
    } catch (const FormatError& error) {
        return Error{ErrorCode::InvalidFormat, error.what()};
    } catch (const Json::exception& error) {
        return Error{ErrorCode::InvalidFormat, error.what()};
    }
}

} // namespace

std::string codeFileName(CodeFile file, ProgramLanguage language) {
    std::string_view extension;
    switch (language) {
    case ProgramLanguage::Blocks:
        return {};
    case ProgramLanguage::Cpp:
        extension = ".cpp";
        break;
    case ProgramLanguage::Python:
        extension = ".py";
        break;
    case ProgramLanguage::JavaScript:
        extension = ".js";
        break;
    }
    return std::string(file == CodeFile::MyCode ? "my_code" : "program") + std::string(extension);
}

std::string toJson(const Project& project) {
    return projectDocument(project, CodeStorage::Inline, nullptr).dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

detail::ArchiveDocument detail::toArchiveDocument(const Project& project) {
    ArchiveDocument document;
    // Text that is not UTF-8 (a Latin-1 name or code file) is replaced, not thrown (audit #187).
    document.json = projectDocument(project, CodeStorage::InArchive, &document.files).dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    return document;
}

Result<Project> detail::fromArchiveDocument(std::string_view json, const Files& files) {
    return parse(json, &files);
}



Result<Project> fromJson(std::string_view text) {
    return parse(text, nullptr);
}

} // namespace s2a::project
