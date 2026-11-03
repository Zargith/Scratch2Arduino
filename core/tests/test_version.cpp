#include "s2a/version.hpp"

#include <gtest/gtest.h>
#include <string>

namespace {

TEST(Version, MatchesComponents) {
    const std::string expected = std::to_string(s2a::version::kMajor) + "." +
                                 std::to_string(s2a::version::kMinor) + "." +
                                 std::to_string(s2a::version::kPatch);
    EXPECT_EQ(expected, s2a::version::kString);
}

TEST(Version, HasAppName) {
    EXPECT_STREQ(s2a::version::kAppName, "Scratch2Arduino");
}

} // namespace
