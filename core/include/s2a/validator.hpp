#pragma once

#include "s2a/blocks.hpp"
#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/project.hpp"

#include <string>
#include <vector>

/// Checks a project before Build/Upload. The editors never call it while the user is building
/// a program: mistakes are only reported when the user asks to check or upload, in words a
/// child understands, with a hint on how to fix them and the block to look at.
namespace s2a::validation {

enum class Severity {
    Error,   ///< The program cannot be built.
    Warning, ///< Built anyway, but probably not what the user wants.
};

enum class Rule {
    NoStartBlock,       ///< No "when the program starts" script.
    EmptyScript,        ///< A hat with nothing below.
    EmptyMouth,         ///< repeat / if / forever with nothing inside.
    BlocksAfterForever, ///< Blocks below "forever" never run.
    MissingCondition,   ///< Empty condition slot.
    InvalidNumber,      ///< A number slot holding something that is not a number.
    InvalidPin,         ///< Pin missing on the board.
    PinCapability,      ///< Pin that cannot do what the block needs (PWM, analog...).
    MissingVariable,    ///< No variable chosen, or a variable that was deleted.
    UnknownBlock,       ///< Block type not in the catalog (project made with a newer version).
    MissingFunction,    ///< No function chosen, or a function that was deleted.
    FunctionNotDefined, ///< "call" of a function that has no "define" script.
    FunctionDefinedTwice, ///< Two "define" scripts for the same function.
    RecursiveFunction,  ///< A function that calls itself (directly or through others): it would never end.
    ParameterOutsideFunction, ///< An input block used outside the function that has this input.
    ButtonWithoutPullUp, ///< "Set pin ... as input" (no pull-up) on the pin of a button wired to GND.
    NoBluetooth,        ///< A phone block on a board without Bluetooth (nor a module).
    ModuleMissing,      ///< Phone blocks on an Arduino board without the "Bluetooth module" block.
    ModuleNotNeeded,    ///< The "Bluetooth module" block on a board that takes no module (ESP32...).
    ModulePins,         ///< The "Bluetooth module" block on pins it cannot use.
    ModuleTwice,        ///< Two "Bluetooth module" blocks on different pins.
    UserCodeMissing,    ///< A "My code" block whose function or variable is not in the code (any more).
    UserCodeNoValue,    ///< The result of a "My code" function that gives no value.
    UserCodeReadOnly,   ///< Setting a constant of "My code".
    UserCodeOwned,      ///< Setting a variable that the functions of "My code" change themselves.
};

struct Issue {
    Rule rule = Rule::NoStartBlock;
    Severity severity = Severity::Error;
    std::string blockId; ///< Block to highlight; empty for project-wide issues.
    TranslationParams params;
};

[[nodiscard]] std::vector<Issue> validate(const project::Project& project, const BlockCatalog& catalog,
                                          const Board* board, Language language);

/// Same, with the built-in catalog and the project's board.
[[nodiscard]] std::vector<Issue> validate(const project::Project& project, Language language);

[[nodiscard]] bool hasErrors(const std::vector<Issue>& issues);

/// "validation.<rule>" - the message - and "validation.<rule>_hint" - how to fix it.
[[nodiscard]] std::string_view ruleKey(Rule rule) noexcept;

/// Message followed by the hint, in `language`.
[[nodiscard]] std::string describe(const Issue& issue, Language language);

} // namespace s2a::validation
