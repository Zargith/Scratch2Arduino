#pragma once

#include "s2a/i18n.hpp"

#include <string>
#include <string_view>

namespace s2a::webview {

/// Builds the self-contained HTML page shown until the block editor lands (Phase 3a).
/// Fallback page shown when the block editor cannot be loaded, explaining why.
[[nodiscard]] std::string welcomePage(Language language, std::string_view message);

} // namespace s2a::webview
