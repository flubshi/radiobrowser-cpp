// SPDX-License-Identifier: MIT

#include <radiobrowser/result.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace radiobrowser {
namespace {

Result<int> half(int value) {
  if (value % 2 != 0) {
    return Error{ErrorCode::InvalidArgument, "odd", 0};
  }
  return value / 2;
}

TEST(ResultTest, HoldsValue) {
  const auto result = half(10);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(static_cast<bool>(result));
  EXPECT_EQ(result.value(), 5);
  EXPECT_EQ(*result, 5);
  EXPECT_EQ(result.value_or(-1), 5);
}

TEST(ResultTest, HoldsError) {
  const auto result = half(3);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(result.error().message, "odd");
  EXPECT_EQ(result.value_or(-1), -1);
}

TEST(ResultTest, WrongAccessThrows) {
  const auto failed = half(3);
  EXPECT_THROW(static_cast<void>(failed.value()), BadResultAccess);
  try {
    static_cast<void>(failed.value());
  } catch (const BadResultAccess& e) {
    EXPECT_EQ(e.code(), ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(e.what()).find("Result"), std::string::npos);
  }

  const auto succeeded = half(4);
  EXPECT_THROW(static_cast<void>(succeeded.error()), BadResultAccess);
}

TEST(ResultTest, SupportsMoveOnlyTypes) {
  Result<std::unique_ptr<int>> result = std::make_unique<int>(7);
  ASSERT_TRUE(result);
  std::unique_ptr<int> value = std::move(result).value();
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 7);
}

TEST(ResultTest, ArrowOperatorAndConversion) {
  Result<std::string> result = std::string("radio");
  EXPECT_EQ(result->size(), 5U);

  Result<std::vector<int>> from_braces = std::vector<int>{1, 2, 3};
  EXPECT_EQ(from_braces->size(), 3U);

  Result<std::string> from_literal = "literal";
  EXPECT_EQ(*from_literal, "literal");
}

TEST(ResultTest, ErrorToString) {
  const Error error{ErrorCode::Timeout, "after 5s", 0};
  EXPECT_EQ(to_string(error), "timeout: after 5s");
  EXPECT_EQ(to_string(Error{ErrorCode::Network, "", 0}), "network error");
  EXPECT_EQ(to_string(ErrorCode::AllServersFailed), "all servers failed");
}

}  // namespace
}  // namespace radiobrowser
