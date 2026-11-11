#pragma once

#include "s2a/project.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// The "My code" file of a project: the functions and global variables it declares become
/// blocks of the "My code" category (call a function, its result, read or set a variable).
///
/// The declarations are found by a small scanner, not a compiler: it never fails. Code it does
/// not understand (a half-typed line, a class, an array) simply gives no block; the compiler or
/// the interpreter reports real mistakes at Check, Upload or Live.
namespace s2a::usercode {

/// Blocks have at most this many number slots: functions with more parameters get no block.
inline constexpr std::size_t kMaxParameters = 4;

struct Declaration {
    enum class Kind { Function, Variable };
    Kind kind = Kind::Function;
    std::string name;
    std::vector<std::string> parameters; ///< Functions: parameter names (numbers), at most kMaxParameters.
    bool returnsValue = false;           ///< Functions: gives a value (a reporter block), else a "call" block.
    bool readOnly = false;               ///< Variables: const, cannot be set by a block.
    bool text = false;                   ///< C++: an Arduino String (variable, or a function giving one).
    /// Variables: changed by a function of the code itself (its state, like "lampe" of a switch).
    /// Blocks may read it but not set it: they would disturb the code.
    bool changedByCode = false;
    int line = 0;                        ///< 1-based line of the declaration.
};

/// The declarations of `code` usable by blocks, in the order of the file (one per name).
/// C++: functions returning void, a number or a String, with number parameters, and number or
/// String variables, at
/// file scope (setup and loop are left out: the blocks make them). Python: top-level def and
/// assignments. JavaScript: top-level function declarations and let/var/const.
[[nodiscard]] std::vector<Declaration> declarations(project::ProgramLanguage language, std::string_view code);

/// Same, for the user code of a project (none when it has no user code).
[[nodiscard]] std::vector<Declaration> declarations(const project::Project& project);

/// The declaration named `name` of kind `kind`, or null.
[[nodiscard]] const Declaration* find(const std::vector<Declaration>& declarations, std::string_view name,
                                      Declaration::Kind kind);

/// Block types of the "My code" category.
inline constexpr std::string_view kCallBlock = "mycode_call";   ///< Statement: runs a function.
inline constexpr std::string_view kValueBlock = "mycode_value"; ///< Reporter: result of a function.
inline constexpr std::string_view kGetBlock = "mycode_get";     ///< Reporter: value of a variable.
inline constexpr std::string_view kSetBlock = "mycode_set";     ///< Statement: sets a variable.
/// Field holding the function or variable name; inputs ARG1..ARG4 hold the parameters, VALUE the new value.
inline constexpr std::string_view kNameField = "NAME";

/// True for the blocks of the "My code" category.
[[nodiscard]] bool isUserCodeBlock(std::string_view type) noexcept;

/// True when a script of the project uses a "My code" block.
[[nodiscard]] bool usesUserCode(const project::Project& project);

} // namespace s2a::usercode
