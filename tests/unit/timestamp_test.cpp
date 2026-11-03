// SPDX-License-Identifier: MIT

#include <radiobrowser/types.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

namespace radiobrowser {
namespace {

std::int64_t seconds_of(const std::optional<Timestamp>& timestamp) {
  return timestamp->time_since_epoch().count();
}

TEST(TimestampTest, ParsesIsoUtc) {
  const auto parsed = parse_timestamp("2019-12-12T18:37:02Z");
  ASSERT_TRUE(parsed);
  EXPECT_EQ(seconds_of(parsed), 1576175822);
}

TEST(TimestampTest, ParsesLegacyFormatAsUtc) {
  const auto parsed = parse_timestamp("2019-12-12 18:37:02");
  ASSERT_TRUE(parsed);
  EXPECT_EQ(seconds_of(parsed), 1576175822);
}

TEST(TimestampTest, ParsesFractionsAndOffsets) {
  EXPECT_EQ(seconds_of(parse_timestamp("2019-12-12T18:37:02.987Z")), 1576175822);
  EXPECT_EQ(seconds_of(parse_timestamp("2019-12-12T20:37:02+02:00")), 1576175822);
  EXPECT_EQ(seconds_of(parse_timestamp("2019-12-12T17:07:02-0130")), 1576175822);
  EXPECT_EQ(seconds_of(parse_timestamp("  2019-12-12t18:37:02z ")), 1576175822);
}

TEST(TimestampTest, ParsesEpochAndLeapDay) {
  EXPECT_EQ(seconds_of(parse_timestamp("1970-01-01T00:00:00Z")), 0);
  EXPECT_EQ(seconds_of(parse_timestamp("2024-02-29T00:00:00Z")), 1709164800);
  EXPECT_EQ(seconds_of(parse_timestamp("1969-12-31T23:59:59Z")), -1);
}

TEST(TimestampTest, RejectsInvalidInput) {
  EXPECT_FALSE(parse_timestamp(""));
  EXPECT_FALSE(parse_timestamp("null"));
  EXPECT_FALSE(parse_timestamp("2019-13-12T18:37:02Z"));
  EXPECT_FALSE(parse_timestamp("2023-02-29T00:00:00Z"));
  EXPECT_FALSE(parse_timestamp("2019-12-12T24:00:00Z"));
  EXPECT_FALSE(parse_timestamp("2019/12/12 18:37:02"));
  EXPECT_FALSE(parse_timestamp("2019-12-12T18:37:02+2"));
  EXPECT_FALSE(parse_timestamp("2019-12-12T18:37:02Zjunk"));
  EXPECT_FALSE(parse_timestamp("2019-12-12T18:37:02."));
}

TEST(TimestampTest, FormatsIso) {
  EXPECT_EQ(format_timestamp(Timestamp(std::chrono::seconds(1576175822))), "2019-12-12T18:37:02Z");
  EXPECT_EQ(format_timestamp(Timestamp(std::chrono::seconds(0))), "1970-01-01T00:00:00Z");
  EXPECT_EQ(format_timestamp(Timestamp(std::chrono::seconds(-1))), "1969-12-31T23:59:59Z");
  EXPECT_EQ(format_timestamp(Timestamp(std::chrono::seconds(1709164800))), "2024-02-29T00:00:00Z");
}

TEST(TimestampTest, RoundTrips) {
  for (const char* text :
       {"2000-02-29T12:00:00Z", "2038-01-19T03:14:08Z", "2099-12-31T23:59:59Z"}) {
    const auto parsed = parse_timestamp(text);
    ASSERT_TRUE(parsed) << text;
    EXPECT_EQ(format_timestamp(*parsed), text);
  }
}

}  // namespace
}  // namespace radiobrowser
