#pragma once

#include "s2a/blocks.hpp"
#include "s2a/boards.hpp"
#include "s2a/project.hpp"

#include <array>
#include <map>
#include <string>
#include <string_view>

/// Turns a block project into an Arduino sketch (.ino).
///
/// The generator never fails: missing or invalid inputs fall back to safe defaults
/// (0, false, the argument's default value). Reporting those mistakes to the user is
/// the job of the validator, which runs before Build/Upload.
///
/// Execution model:
/// - only scripts starting with a hat block run; loose blocks are ignored;
/// - the blocks of every script run once, in order, in setup();
/// - when a script ends with "forever", the body of that loop goes into loop();
///   several such scripts share loop() and take turns.
namespace s2a::codegen {

struct Sketch {
    std::string source;                    ///< Complete .ino file.
    std::map<std::string, int> blockLines; ///< Block id -> first line (1-based) of its code.
};

[[nodiscard]] Sketch generate(const project::Project& project, const BlockCatalog& catalog,
                              const Board* board);

/// Same, with the built-in catalog and the project's board from the built-in database.
[[nodiscard]] Sketch generate(const project::Project& project);

/// Speed of the serial monitor used by the "write to the serial monitor" block.
inline constexpr int kSerialBaud = 9600;

/// Red, green and blue levels (0-255) of a colour of the RGB LED menu ("RED"...); off when unknown.
[[nodiscard]] std::array<int, 3> colourLevels(std::string_view name);

/// Rows (top to bottom, left column = high bit) of a picture of the LED matrix menu; empty when unknown.
[[nodiscard]] std::array<int, 8> matrixImage(std::string_view name);

/// Text for the screens, which only show ASCII: accents removed ("élève" -> "eleve"), other
/// characters replaced by '?', control characters dropped.
[[nodiscard]] std::string asciiText(std::string_view text);

/// Text typed in a slot as a C++ string literal (quotes and backslashes escaped, control
/// characters except tab and newline dropped).
[[nodiscard]] std::string stringLiteral(std::string_view text);

/// Number typed in a slot ("1,5", " 3 ", "abc") as C++ source; "0" when invalid.
[[nodiscard]] std::string numberLiteral(const std::string& text);

/// Valid C++ identifier derived from a variable name ("vitesse max" -> "vitesse_max").
[[nodiscard]] std::string identifier(const std::string& name);

} // namespace s2a::codegen
