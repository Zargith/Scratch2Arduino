#pragma once

#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/result.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace s2a::project {

/// Identifies project.json files and their format version.
inline constexpr std::string_view kFormatName = "scratch2arduino-project";
/// Newest format this version reads. A project is written with the oldest version able to hold
/// it: block projects stay at 1 (older Scratch2Arduino versions open them), projects written as
/// text (version 2) are refused by those versions with a clear "update" message instead of
/// being opened as an empty block project.
inline constexpr int kFormatVersion = 2;
/// Name of the JSON document inside a .s2a archive.
inline constexpr std::string_view kProjectEntry = "project.json";
/// Prefix of the asset entries inside a .s2a archive (images, sounds, saved blocks...).
inline constexpr std::string_view kAssetsPrefix = "assets/";

/// Nullable owning pointer with value semantics (deep copy). Lets Block contain blocks.
template <typename T> class Box {
public:
    Box() = default;
    Box(T value) : ptr_(std::make_unique<T>(std::move(value))) {} // NOLINT(google-explicit-constructor)
    Box(const Box& other) : ptr_(other.ptr_ ? std::make_unique<T>(*other.ptr_) : nullptr) {}
    Box(Box&&) noexcept = default;
    Box& operator=(const Box& other) {
        if (this != &other) {
            ptr_ = other.ptr_ ? std::make_unique<T>(*other.ptr_) : nullptr;
        }
        return *this;
    }
    Box& operator=(Box&&) noexcept = default;
    ~Box() = default;

    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] T* get() const noexcept { return ptr_.get(); }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_.get(); }
    void reset() noexcept { ptr_.reset(); }

private:
    std::unique_ptr<T> ptr_;
};

struct Block;

/// Blocks attached one below the other (a script, or the inside of a C-block).
using Stack = std::vector<Block>;

/// Content of a number, text or condition slot.
struct Input {
    std::string value; ///< What the user typed in the slot (shown when no block is plugged in).
    Box<Block> block;  ///< Reporter or boolean block plugged into the slot, if any.
};

/// One block of a script. Arguments are stored by name, as declared in the block catalog.
/// Projects may be incomplete or reference unknown block types: nothing here is checked
/// while editing; the validator reports problems at Build/Run.
struct Block {
    std::string id;                            ///< Unique within the project.
    std::string type;                          ///< Block catalog type, e.g. "pins_digital_write".
    std::map<std::string, std::string> fields; ///< Pin, dropdown and variable arguments.
    std::map<std::string, Input> inputs;       ///< Number, text and boolean arguments.
    std::map<std::string, Stack> statements;   ///< Stacks inside C-blocks.
};

/// A stack of blocks placed on the workspace.
struct Script {
    double x = 0;
    double y = 0;
    Stack blocks;
};

struct Variable {
    std::string id;
    std::string name;
    double initialValue = 0;
};

/// A function made by the user: a "define" script, run by "call" blocks.
struct Function {
    std::string id;
    std::string name;
};

/// How the program of a project is written.
enum class ProgramLanguage {
    Blocks,     ///< Scratch-style blocks (the scripts).
    Cpp,        ///< Arduino C++ sketch typed by the user (compiled and uploaded like the blocks).
    Python,     ///< Python run on the computer, driving the board through the Live mode.
    JavaScript, ///< JavaScript run on the computer, driving the board through the Live mode.
};

/// "blocks", "cpp", "python", "javascript" (the value stored in project.json).
[[nodiscard]] std::string_view programLanguageId(ProgramLanguage language) noexcept;
/// The language of an id of programLanguageId(); nullopt when unknown.
[[nodiscard]] std::optional<ProgramLanguage> programLanguageFromId(std::string_view id) noexcept;

/// Code written by the user beside the blocks (the "My code" file): its functions and global
/// variables become blocks of the "My code" category. C++ is compiled with the blocks (Upload);
/// Python and JavaScript run on the computer with the blocks (Live mode).
struct UserCode {
    ProgramLanguage language = ProgramLanguage::Cpp; ///< Cpp, Python or JavaScript.
    std::string code;
};

struct Project {
    std::string name;
    std::string boardId{BoardDatabase::kDefaultBoardId};
    std::vector<Variable> variables;
    std::vector<Function> functions;
    std::vector<Script> scripts;
    /// Language the program is written in. A text project keeps its blocks (scripts): going back
    /// to the blocks finds them as they were.
    ProgramLanguage language = ProgramLanguage::Blocks;
    /// The program of a text project (C++, Python or JavaScript source), empty for blocks.
    std::string code;
    /// The user's own code file, used with the blocks (block projects only).
    std::optional<UserCode> userCode;
    /// Extra files stored in the archive under assets/ (path without the prefix -> bytes).
    std::map<std::string, std::string> assets;
};

/// A new project: default board and a "when the program starts" block, named in `language`.
[[nodiscard]] Project newProject(Language language);

/// A random identifier for blocks and variables.
[[nodiscard]] std::string newId();

/// Calls `visit(const Block&)` for every block of the project, scripts in order, depth first.
template <typename Visitor> void forEachBlock(const Stack& stack, Visitor&& visit);

template <typename Visitor> void forEachBlock(const Block& block, Visitor&& visit) {
    visit(block);
    for (const auto& [name, input] : block.inputs) {
        if (input.block) {
            forEachBlock(*input.block, visit);
        }
    }
    for (const auto& [name, stack] : block.statements) {
        forEachBlock(stack, visit);
    }
}

template <typename Visitor> void forEachBlock(const Stack& stack, Visitor&& visit) {
    for (const Block& block : stack) {
        forEachBlock(block, visit);
    }
}

template <typename Visitor> void forEachBlock(const Project& project, Visitor&& visit) {
    for (const Script& script : project.scripts) {
        forEachBlock(script.blocks, visit);
    }
}

// --- project.json --------------------------------------------------------------------

/// Serializes the project (without assets) to project.json. Deterministic: the same project
/// always gives the same bytes (sorted keys, 2-space indentation, final newline).
[[nodiscard]] std::string toJson(const Project& project);

/// Parses project.json, upgrading older format versions. Only the structure is checked.
[[nodiscard]] Result<Project> fromJson(std::string_view json);

// --- .s2a archives ---------------------------------------------------------------------

/// Builds the bytes of a .s2a archive (zip: project.json + assets/). Deterministic.
[[nodiscard]] Result<std::string> toArchive(const Project& project);

/// Reads a .s2a archive from memory.
[[nodiscard]] Result<Project> fromArchive(std::string_view bytes);

/// Saves a .s2a file. Writes a temporary file first, so an existing project is never left
/// half-written.
[[nodiscard]] Status save(const Project& project, const std::filesystem::path& path);

/// Opens a .s2a file.
/// Reads a .s2a archive, or a bare project.json (as in examples/projects).
[[nodiscard]] Result<Project> load(const std::filesystem::path& path);

} // namespace s2a::project
