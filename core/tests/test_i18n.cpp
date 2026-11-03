#include "s2a/i18n.hpp"

#include <gtest/gtest.h>

namespace {

using s2a::Language;

TEST(LanguageFromLocale, RecognisesEnglishVariants) {
    EXPECT_EQ(s2a::languageFromLocale("en"), Language::English);
    EXPECT_EQ(s2a::languageFromLocale("en_US.UTF-8"), Language::English);
    EXPECT_EQ(s2a::languageFromLocale("en-GB"), Language::English);
    EXPECT_EQ(s2a::languageFromLocale("EN_us"), Language::English);
    EXPECT_EQ(s2a::languageFromLocale("en@euro"), Language::English);
}

TEST(LanguageFromLocale, RecognisesFrench) {
    EXPECT_EQ(s2a::languageFromLocale("fr"), Language::French);
    EXPECT_EQ(s2a::languageFromLocale("fr_FR.UTF-8"), Language::French);
    EXPECT_EQ(s2a::languageFromLocale("fr-CA"), Language::French);
}

TEST(LanguageFromLocale, FallsBackToFrench) {
    EXPECT_EQ(s2a::languageFromLocale(""), Language::French);
    EXPECT_EQ(s2a::languageFromLocale("C"), Language::French);
    EXPECT_EQ(s2a::languageFromLocale("POSIX"), Language::French);
    EXPECT_EQ(s2a::languageFromLocale("C.UTF-8"), Language::French);
    EXPECT_EQ(s2a::languageFromLocale("de_DE.UTF-8"), Language::French);
    EXPECT_EQ(s2a::languageFromLocale("eng"), Language::French);
}

TEST(LanguageCode, ReturnsIsoCodes) {
    EXPECT_EQ(s2a::languageCode(Language::French), "fr");
    EXPECT_EQ(s2a::languageCode(Language::English), "en");
}

TEST(Translator, FlattensNestedObjects) {
    s2a::Translator translator;
    ASSERT_TRUE(translator.load(Language::French, R"({"a": {"b": {"c": "valeur"}}, "d": "x"})").ok());
    EXPECT_EQ(translator.tr("a.b.c", Language::French), "valeur");
    EXPECT_EQ(translator.tr("d", Language::French), "x");
    EXPECT_EQ(translator.keys(Language::French), (std::vector<std::string>{"a.b.c", "d"}));
}

TEST(Translator, FallsBackToFrenchThenToTheKey) {
    s2a::Translator translator;
    ASSERT_TRUE(translator.load(Language::French, R"({"only_fr": "bonjour"})").ok());
    ASSERT_TRUE(translator.load(Language::English, R"({"both": "hello"})").ok());
    EXPECT_EQ(translator.tr("both", Language::English), "hello");
    EXPECT_EQ(translator.tr("only_fr", Language::English), "bonjour");
    EXPECT_EQ(translator.tr("missing.key", Language::English), "missing.key");
}

TEST(Translator, SubstitutesParameters) {
    s2a::Translator translator;
    ASSERT_TRUE(translator.load(Language::French, R"j({"pin": "Broche {pin} : {pin} ({state})"})j").ok());
    EXPECT_EQ(translator.tr("pin", Language::French, {{"pin", "13"}, {"state", "HAUT"}}),
              "Broche 13 : 13 (HAUT)");
    EXPECT_EQ(translator.tr("pin", Language::French), "Broche {pin} : {pin} ({state})");
}

TEST(Translator, ParameterValueContainingThePlaceholderDoesNotLoop) {
    s2a::Translator translator;
    ASSERT_TRUE(translator.load(Language::French, R"({"k": "{x}"})").ok());
    EXPECT_EQ(translator.tr("k", Language::French, {{"x", "{x}{x}"}}), "{x}{x}");
}

TEST(Translator, RejectsInvalidCatalogs) {
    s2a::Translator translator;
    const auto broken = translator.load(Language::French, "{not json");
    ASSERT_FALSE(broken.ok());
    EXPECT_EQ(broken.error().code, s2a::ErrorCode::InvalidJson);
    const auto notAnObject = translator.load(Language::French, R"(["a"])");
    ASSERT_FALSE(notAnObject.ok());
    EXPECT_EQ(notAnObject.error().code, s2a::ErrorCode::InvalidFormat);
}

TEST(DefaultTranslator, LoadsEmbeddedCatalogs) {
    EXPECT_EQ(s2a::tr("app.title", Language::French), "Scratch2Arduino");
    EXPECT_EQ(s2a::tr("app.coming_soon", Language::French), "L'éditeur de blocs arrive bientôt.");
    EXPECT_EQ(s2a::tr("app.coming_soon", Language::English), "The block editor is coming soon.");
}

// Every string must exist in both languages: a missing English entry would silently show French.
TEST(DefaultTranslator, CatalogsHaveTheSameKeys) {
    const auto& translator = s2a::defaultTranslator();
    const auto french = translator.keys(Language::French);
    const auto english = translator.keys(Language::English);
    EXPECT_FALSE(french.empty());
    EXPECT_EQ(french, english);
}

TEST(DefaultTranslator, EveryErrorCodeHasAMessage) {
    using s2a::ErrorCode;
    for (const ErrorCode code :
         {ErrorCode::FileNotFound, ErrorCode::ReadFailed, ErrorCode::WriteFailed,
          ErrorCode::InvalidArchive, ErrorCode::InvalidJson, ErrorCode::InvalidFormat,
          ErrorCode::UnsupportedVersion, ErrorCode::InvalidCatalog, ErrorCode::UnknownBoard, ErrorCode::ProcessFailed}) {
        for (const Language language : s2a::kLanguages) {
            EXPECT_TRUE(s2a::defaultTranslator().has(s2a::errorMessageKey(code), language))
                << s2a::errorMessageKey(code);
        }
    }
}

} // namespace
