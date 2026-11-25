// Parsing JSON from files the user opens (private to the core).
#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace s2a::detail {

/// Deepest nesting accepted in a project or blocks file. The parser itself does not recurse,
/// but copying, dumping and reading the blocks do: a file nested a million levels deep would
/// overflow the stack (audit 2026-11-24). Real projects stay far below (a block is about four
/// levels, and blocks nest at most 256 deep).
inline constexpr int kMaxJsonDepth = 2048;

/// Parses `text` without throwing. Gives a discarded value when the text is not valid JSON or
/// nests deeper than kMaxJsonDepth.
[[nodiscard]] inline nlohmann::json parseUntrustedJson(std::string_view text) {
    bool tooDeep = false;
    const auto limitDepth = [&tooDeep](int depth, nlohmann::json::parse_event_t event, nlohmann::json& /*parsed*/) {
        if ((event == nlohmann::json::parse_event_t::object_start ||
             event == nlohmann::json::parse_event_t::array_start) &&
            depth > kMaxJsonDepth) {
            tooDeep = true;
            return false; // drop it: the whole text is refused below
        }
        return true;
    };
    nlohmann::json json = nlohmann::json::parse(text, limitDepth, /*allow_exceptions=*/false);
    return tooDeep ? nlohmann::json(nlohmann::json::value_t::discarded) : json;
}

} // namespace s2a::detail
