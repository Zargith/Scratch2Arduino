#pragma once

#include "s2a/i18n.hpp"

#include <string>

namespace s2a::webview {

/// Builds the self-contained HTML page shown until the block editor lands (Phase 3a).
[[nodiscard]] std::string welcomePage(Language language);

} // namespace s2a::webview
