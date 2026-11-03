// SPDX-License-Identifier: MIT
// Locale independent ISO 8601 parsing and formatting without gmtime/timegm,
// which are not thread-safe or not portable.

#include <radiobrowser/types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace radiobrowser {

namespace {

constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kSecondsPerHour = 3600;
constexpr std::int64_t kSecondsPerDay = 86400;

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
[[nodiscard]] constexpr std::int64_t days_from_civil(std::int64_t year, std::int64_t month,
                                                     std::int64_t day) noexcept {
  year -= month <= 2 ? 1 : 0;
  const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
  const std::int64_t year_of_era = year - era * 400;
  const std::int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const std::int64_t day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + day_of_era - 719468;
}

struct CivilDate {
  std::int64_t year;
  std::int64_t month;
  std::int64_t day;
};

// Inverse of days_from_civil.
[[nodiscard]] constexpr CivilDate civil_from_days(std::int64_t days) noexcept {
  days += 719468;
  const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const std::int64_t day_of_era = days - era * 146097;
  const std::int64_t year_of_era =
      (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  const std::int64_t day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const std::int64_t mp = (5 * day_of_year + 2) / 153;
  const std::int64_t day = day_of_year - (153 * mp + 2) / 5 + 1;
  const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
  return CivilDate{year_of_era + era * 400 + (month <= 2 ? 1 : 0), month, day};
}

[[nodiscard]] constexpr bool is_leap_year(std::int64_t year) noexcept {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

[[nodiscard]] constexpr std::int64_t days_in_month(std::int64_t year, std::int64_t month) noexcept {
  switch (month) {
    case 2:
      return is_leap_year(year) ? 29 : 28;
    case 4:
    case 6:
    case 9:
    case 11:
      return 30;
    default:
      return 31;
  }
}

// Reads exactly `count` decimal digits starting at `pos`.
[[nodiscard]] std::optional<std::int64_t> read_digits(std::string_view text, std::size_t pos,
                                                      std::size_t count) noexcept {
  if (pos + count > text.size()) {
    return std::nullopt;
  }
  std::int64_t value = 0;
  for (std::size_t i = pos; i < pos + count; ++i) {
    const char c = text[i];
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + (c - '0');
  }
  return value;
}

[[nodiscard]] bool expect_char(std::string_view text, std::size_t pos, char expected) noexcept {
  return pos < text.size() && text[pos] == expected;
}

void append_padded(std::string& out, std::int64_t value, std::size_t width) {
  const std::string digits = std::to_string(value);
  if (digits.size() < width) {
    out.append(width - digits.size(), '0');
  }
  out += digits;
}

}  // namespace

std::optional<Timestamp> parse_timestamp(std::string_view text) {
  // Trim surrounding whitespace.
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.remove_suffix(1);
  }
  // Minimum: "YYYY-MM-DD HH:MM:SS".
  constexpr std::size_t kMinimumLength = 19;
  if (text.size() < kMinimumLength) {
    return std::nullopt;
  }

  const auto year = read_digits(text, 0, 4);
  const auto month = read_digits(text, 5, 2);
  const auto day = read_digits(text, 8, 2);
  const auto hour = read_digits(text, 11, 2);
  const auto minute = read_digits(text, 14, 2);
  const auto second = read_digits(text, 17, 2);
  const bool separators_ok =
      expect_char(text, 4, '-') && expect_char(text, 7, '-') &&
      (expect_char(text, 10, 'T') || expect_char(text, 10, 't') || expect_char(text, 10, ' ')) &&
      expect_char(text, 13, ':') && expect_char(text, 16, ':');
  if (!separators_ok || !year || !month || !day || !hour || !minute || !second) {
    return std::nullopt;
  }
  const CivilDate date{*year, *month, *day};
  const std::int64_t seconds_of_day =
      *hour * kSecondsPerHour + *minute * kSecondsPerMinute + *second;
  if (date.month < 1 || date.month > 12 || date.day < 1 ||
      date.day > days_in_month(date.year, date.month) || *hour > 23 || *minute > 59 ||
      *second > 60) {
    return std::nullopt;
  }

  std::size_t pos = kMinimumLength;
  // Optional fractional seconds; precision beyond seconds is dropped.
  if (pos < text.size() && (text[pos] == '.' || text[pos] == ',')) {
    ++pos;
    const std::size_t fraction_start = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
      ++pos;
    }
    if (pos == fraction_start) {
      return std::nullopt;
    }
  }

  // Optional UTC designator or offset; no designator means UTC.
  std::int64_t offset_seconds = 0;
  if (pos < text.size()) {
    const char designator = text[pos];
    if (designator == 'Z' || designator == 'z') {
      ++pos;
    } else if (designator == '+' || designator == '-') {
      const auto offset_hours = read_digits(text, pos + 1, 2);
      std::optional<std::int64_t> offset_minutes;
      std::size_t end = pos + 3;
      if (expect_char(text, end, ':')) {
        offset_minutes = read_digits(text, end + 1, 2);
        end += 3;
      } else {
        offset_minutes = read_digits(text, end, 2);
        end += 2;
      }
      if (!offset_hours || !offset_minutes || *offset_hours > 23 || *offset_minutes > 59) {
        return std::nullopt;
      }
      offset_seconds = *offset_hours * kSecondsPerHour + *offset_minutes * kSecondsPerMinute;
      if (designator == '-') {
        offset_seconds = -offset_seconds;
      }
      pos = end;
    } else {
      return std::nullopt;
    }
  }
  if (pos != text.size()) {
    return std::nullopt;
  }

  const std::int64_t seconds = days_from_civil(date.year, date.month, date.day) * kSecondsPerDay +
                               seconds_of_day - offset_seconds;
  return Timestamp(std::chrono::seconds(seconds));
}

std::string format_timestamp(Timestamp timestamp) {
  const std::int64_t total = timestamp.time_since_epoch().count();
  std::int64_t days = total / kSecondsPerDay;
  std::int64_t seconds_of_day = total % kSecondsPerDay;
  if (seconds_of_day < 0) {
    seconds_of_day += kSecondsPerDay;
    --days;
  }
  const CivilDate date = civil_from_days(days);

  std::string out;
  out.reserve(20);
  append_padded(out, date.year, 4);
  out.push_back('-');
  append_padded(out, date.month, 2);
  out.push_back('-');
  append_padded(out, date.day, 2);
  out.push_back('T');
  append_padded(out, seconds_of_day / kSecondsPerHour, 2);
  out.push_back(':');
  append_padded(out, (seconds_of_day % kSecondsPerHour) / kSecondsPerMinute, 2);
  out.push_back(':');
  append_padded(out, seconds_of_day % kSecondsPerMinute, 2);
  out.push_back('Z');
  return out;
}

}  // namespace radiobrowser
