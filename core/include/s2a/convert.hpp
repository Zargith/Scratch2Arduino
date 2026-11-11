#pragma once

#include "s2a/project.hpp"

#include <string>
#include <string_view>
#include <vector>

/// Converts a program between the text languages of Scratch2Arduino: Arduino C++, Python and
/// JavaScript (the "My code" files and the whole-program expert modes).
///
/// It handles what beginners write: global variables and constants, functions with number
/// parameters, if / else if / else, while, counting for loops (range() in Python), loops over a
/// list of values, return / break / continue, arithmetic, comparisons, logic, the conditional
/// expression, comments, and the Arduino functions (the same names in the three languages;
/// print() <-> Serial.println(), time.sleep(s) <-> delay(s * 1000)).
///
/// It never fails: a line it does not understand is kept as a comment to convert by hand, and its
/// number is given in `toCheck`. It is a translator for learning, not a compiler: C++ types are
/// guessed (int, float, String, bool), and integer division may differ between languages.
namespace s2a::convert {

struct Converted {
    std::string code;
    /// Lines of the source (1-based) that were kept as comments, to convert by hand.
    std::vector<int> toCheck;
};

/// `source` written in `from`, rewritten in `to` (Cpp, Python or JavaScript; Blocks is not a
/// text language and gives the source back unchanged).
[[nodiscard]] Converted convert(std::string_view source, project::ProgramLanguage from, project::ProgramLanguage to);

} // namespace s2a::convert
