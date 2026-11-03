#include "s2a/i18n.hpp"

#include <array>
#include <cctype>
#include <cstddef>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace s2a {

namespace {

struct Translation {
    Text key;
    std::string_view french;
    std::string_view english;
};

constexpr std::array<Translation, 5> kTranslations{{
    {Text::AppTitle, "Scratch2Arduino", "Scratch2Arduino"},
    {Text::Welcome,
     "Bienvenue dans Scratch2Arduino ! Branche ta carte Arduino et commence à assembler des "
     "blocs.",
     "Welcome to Scratch2Arduino! Plug in your Arduino board and start snapping blocks "
     "together."},
    {Text::ComingSoon, "L'éditeur de blocs arrive bientôt.", "The block editor is coming soon."},
    {Text::VariantWebview, "Variante A — webview", "Variant A — webview"},
    {Text::VariantNative, "Variante B — native (Qt)", "Variant B — native (Qt)"},
}};

char toLowerAscii(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

} // namespace

Language languageFromLocale(std::string_view locale) noexcept {
    if (locale.size() < 2) {
        return kDefaultLanguage;
    }
    // The language is the part before '_', '-', '.' or '@' ("en_US.UTF-8" -> "en").
    const std::size_t end = locale.find_first_of("_-.@");
    const std::string_view code = locale.substr(0, end);
    if (code.size() != 2) {
        return kDefaultLanguage;
    }
    const char first = toLowerAscii(code[0]);
    const char second = toLowerAscii(code[1]);
    if (first == 'e' && second == 'n') {
        return Language::English;
    }
    return kDefaultLanguage;
}

Language detectSystemLanguage() {
#ifdef _WIN32
    const LANGID langId = GetUserDefaultUILanguage();
    return PRIMARYLANGID(langId) == LANG_ENGLISH ? Language::English : kDefaultLanguage;
#else
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(variable); // NOLINT(concurrency-mt-unsafe): read at startup
        if (value != nullptr && *value != '\0') {
            return languageFromLocale(value);
        }
    }
    return kDefaultLanguage;
#endif
}

std::string_view languageCode(Language language) noexcept {
    return language == Language::English ? "en" : "fr";
}

std::string_view translate(Text text, Language language) noexcept {
    for (const auto& entry : kTranslations) {
        if (entry.key == text) {
            return language == Language::English ? entry.english : entry.french;
        }
    }
    return {};
}

} // namespace s2a
