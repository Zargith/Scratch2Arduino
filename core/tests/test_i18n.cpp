#include "s2a/i18n.hpp"

#include <gtest/gtest.h>

namespace {

using s2a::Language;
using s2a::Text;

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

TEST(Translate, EveryTextExistsInBothLanguages) {
    for (const Text text : {Text::AppTitle, Text::Welcome, Text::ComingSoon, Text::VariantWebview,
                            Text::VariantNative}) {
        EXPECT_FALSE(s2a::translate(text, Language::French).empty());
        EXPECT_FALSE(s2a::translate(text, Language::English).empty());
    }
}

TEST(Translate, ReturnsTheRequestedLanguage) {
    EXPECT_EQ(s2a::translate(Text::ComingSoon, Language::French),
              "L'éditeur de blocs arrive bientôt.");
    EXPECT_EQ(s2a::translate(Text::ComingSoon, Language::English),
              "The block editor is coming soon.");
}

} // namespace
