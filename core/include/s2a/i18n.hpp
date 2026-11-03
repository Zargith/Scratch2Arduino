#pragma once

#include "s2a/result.hpp"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace s2a {

/// Interface languages. French is the default, English the second language.
enum class Language { French, English };

inline constexpr Language kDefaultLanguage = Language::French;
inline constexpr std::array<Language, 2> kLanguages{Language::French, Language::English};

/// Parses a locale identifier such as "fr_FR.UTF-8", "en-US" or "en".
/// Anything that is not recognised (empty, "C", "POSIX", other languages) gives the default.
[[nodiscard]] Language languageFromLocale(std::string_view locale) noexcept;

/// Detects the user's language: LC_ALL, LC_MESSAGES then LANG on POSIX systems,
/// the user UI language on Windows. Falls back to the default language.
[[nodiscard]] Language detectSystemLanguage();

/// ISO 639-1 code of a language ("fr", "en"), also the name of its catalog file.
[[nodiscard]] std::string_view languageCode(Language language) noexcept;

/// Values substituted for "{name}" placeholders in a translation.
using TranslationParams = std::vector<std::pair<std::string, std::string>>;

/// Translation tables, one per language, loaded from JSON catalogs (resources/i18n/<code>.json).
/// Nested objects are flattened into dotted keys: {"app": {"title": "..."}} -> "app.title".
class Translator {
public:
    /// Adds (or overrides) the entries of a JSON catalog for `language`.
    Status load(Language language, std::string_view json);

    /// Translation of `key`. Never fails: falls back to French, then to the key itself,
    /// so a missing translation never breaks the interface.
    [[nodiscard]] std::string tr(std::string_view key, Language language,
                                 const TranslationParams& params = {}) const;

    [[nodiscard]] bool has(std::string_view key, Language language) const;

    /// Every key defined for `language`, sorted.
    [[nodiscard]] std::vector<std::string> keys(Language language) const;

private:
    using Table = std::map<std::string, std::string, std::less<>>;
    [[nodiscard]] const Table& table(Language language) const;
    [[nodiscard]] Table& table(Language language);

    std::array<Table, kLanguages.size()> tables_;
};

/// Translator loaded with the catalogs embedded in the application.
[[nodiscard]] const Translator& defaultTranslator();

/// Shortcut for defaultTranslator().tr(...).
[[nodiscard]] std::string tr(std::string_view key, Language language,
                             const TranslationParams& params = {});

} // namespace s2a
