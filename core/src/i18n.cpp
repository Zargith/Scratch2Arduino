#include "s2a/i18n.hpp"

#include "s2a/resources.hpp"

#include <cctype>
#include <cstddef>
#include <nlohmann/json.hpp>

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

using Json = nlohmann::json;

char toLowerAscii(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::size_t indexOf(Language language) noexcept {
    return language == Language::English ? 1U : 0U;
}

void flatten(const Json& node, const std::string& prefix, std::map<std::string, std::string, std::less<>>& out) {
    for (const auto& [name, value] : node.items()) {
        const std::string key = prefix.empty() ? name : prefix + "." + name;
        if (value.is_object()) {
            flatten(value, key, out);
        } else if (value.is_string()) {
            out[key] = value.get<std::string>();
        }
    }
}

std::string substitute(std::string text, const TranslationParams& params) {
    for (const auto& [name, value] : params) {
        const std::string placeholder = "{" + name + "}";
        for (std::size_t pos = text.find(placeholder); pos != std::string::npos;
             pos = text.find(placeholder, pos + value.size())) {
            text.replace(pos, placeholder.size(), value);
        }
    }
    return text;
}

} // namespace

Language languageFromLocale(std::string_view locale) noexcept {
    // The language is the part before '_', '-', '.' or '@' ("en_US.UTF-8" -> "en").
    const std::string_view code = locale.substr(0, locale.find_first_of("_-.@"));
    if (code.size() != 2) {
        return kDefaultLanguage;
    }
    if (toLowerAscii(code[0]) == 'e' && toLowerAscii(code[1]) == 'n') {
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

Status Translator::load(Language language, std::string_view json) {
    const Json document = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded()) {
        return Error{ErrorCode::InvalidJson,
                     "translation catalog '" + std::string(languageCode(language)) + "' is not valid JSON"};
    }
    if (!document.is_object()) {
        return Error{ErrorCode::InvalidFormat, "a translation catalog must be a JSON object"};
    }
    flatten(document, "", table(language));
    return success();
}

std::string Translator::tr(std::string_view key, Language language,
                           const TranslationParams& params) const {
    for (const Language candidate : {language, kDefaultLanguage}) {
        const Table& entries = table(candidate);
        if (const auto it = entries.find(key); it != entries.end()) {
            return substitute(it->second, params);
        }
    }
    return std::string(key);
}

bool Translator::has(std::string_view key, Language language) const {
    const Table& entries = table(language);
    return entries.find(key) != entries.end();
}

std::vector<std::string> Translator::keys(Language language) const {
    std::vector<std::string> result;
    for (const auto& [key, value] : table(language)) {
        result.push_back(key);
    }
    return result;
}

const Translator::Table& Translator::table(Language language) const {
    return tables_.at(indexOf(language));
}

Translator::Table& Translator::table(Language language) {
    return tables_.at(indexOf(language));
}

const Translator& defaultTranslator() {
    static const Translator translator = [] {
        Translator loaded;
        for (const Language language : kLanguages) {
            const std::string path = "resources/i18n/" + std::string(languageCode(language)) + ".json";
            if (const auto data = resources::find(path)) {
                // Embedded catalogs are checked by the unit tests; a broken one only loses strings.
                (void)loaded.load(language, *data);
            }
        }
        return loaded;
    }();
    return translator;
}

std::string tr(std::string_view key, Language language, const TranslationParams& params) {
    return defaultTranslator().tr(key, language, params);
}

} // namespace s2a
