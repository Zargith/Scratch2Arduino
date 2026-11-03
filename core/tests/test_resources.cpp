#include "s2a/resources.hpp"

#include <algorithm>
#include <gtest/gtest.h>

namespace {

TEST(Resources, EmbedsTheTranslationCatalogs) {
    const auto french = s2a::resources::find("resources/i18n/fr.json");
    ASSERT_TRUE(french.has_value());
    EXPECT_NE(french->find("Scratch2Arduino"), std::string_view::npos);
    EXPECT_TRUE(s2a::resources::find("resources/i18n/en.json").has_value());
}

TEST(Resources, UnknownPathIsNotFound) {
    EXPECT_FALSE(s2a::resources::find("does/not/exist.json").has_value());
    EXPECT_FALSE(s2a::resources::find("").has_value());
}

TEST(Resources, AllIsSortedByPath) {
    const auto& all = s2a::resources::all();
    EXPECT_TRUE(std::is_sorted(all.begin(), all.end(),
                               [](const auto& a, const auto& b) { return a.path < b.path; }));
}

TEST(Resources, ListFiltersByPrefix) {
    const auto catalogs = s2a::resources::list("resources/i18n/");
    ASSERT_EQ(catalogs.size(), 2U);
    EXPECT_EQ(catalogs[0].path, "resources/i18n/en.json");
    EXPECT_EQ(catalogs[1].path, "resources/i18n/fr.json");
    EXPECT_TRUE(s2a::resources::list("nothing/").empty());
}

} // namespace
