#pragma once

#include <string_view>

namespace s2a {

/// Interface languages. French is the default, English the second language.
enum class Language { French, English };

inline constexpr Language kDefaultLanguage = Language::French;

/// Parses a locale identifier such as "fr_FR.UTF-8", "en-US" or "en".
/// Anything that is not recognised (empty, "C", "POSIX", other languages) gives the default.
[[nodiscard]] Language languageFromLocale(std::string_view locale) noexcept;

/// Detects the user's language: LC_ALL, LC_MESSAGES then LANG on POSIX systems,
/// the user UI language on Windows. Falls back to the default language.
[[nodiscard]] Language detectSystemLanguage();

/// ISO 639-1 code of a language ("fr", "en").
[[nodiscard]] std::string_view languageCode(Language language) noexcept;

/// Interface strings needed by the Phase 0 windows.
/// Replaced by JSON catalogs in Phase 1.
enum class Text { AppTitle, Welcome, ComingSoon, VariantWebview, VariantNative };

/// Returns the UTF-8 translation of `text` in `language`.
[[nodiscard]] std::string_view translate(Text text, Language language) noexcept;

} // namespace s2a
