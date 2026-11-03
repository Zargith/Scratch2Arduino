#include "s2a/result.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace {

s2a::Result<int> half(int value) {
    if (value % 2 != 0) {
        return s2a::Error{s2a::ErrorCode::InvalidFormat, "odd"};
    }
    return value / 2;
}

TEST(Result, HoldsAValue) {
    const auto result = half(8);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), 4);
}

TEST(Result, HoldsAnError) {
    const auto result = half(3);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, s2a::ErrorCode::InvalidFormat);
    EXPECT_EQ(result.error().detail, "odd");
}

TEST(Result, SupportsMoveOnlyValues) {
    s2a::Result<std::unique_ptr<std::string>> result = std::make_unique<std::string>("ok");
    std::unique_ptr<std::string> value = std::move(result).value();
    EXPECT_EQ(*value, "ok");
}

TEST(Status, Success) {
    EXPECT_TRUE(s2a::success().ok());
}

TEST(ErrorMessageKey, IsNamespaced) {
    EXPECT_EQ(s2a::errorMessageKey(s2a::ErrorCode::FileNotFound), "error.file_not_found");
    EXPECT_EQ(s2a::errorMessageKey(s2a::ErrorCode::UnsupportedVersion), "error.unsupported_version");
}

} // namespace
