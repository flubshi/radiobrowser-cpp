// SPDX-License-Identifier: MIT

#include "detail/url.hpp"

#include <array>
#include <cstddef>

namespace radiobrowser::detail {

namespace {

[[nodiscard]] constexpr bool is_unreserved(unsigned char c) noexcept {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
         c == '.' || c == '_' || c == '~';
}

}  // namespace

std::string percent_encode(std::string_view text) {
  constexpr std::array<char, 16> kHex{'0', '1', '2', '3', '4', '5', '6', '7',
                                      '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string encoded;
  encoded.reserve(text.size());
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    if (is_unreserved(c)) {
      encoded.push_back(raw);
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex.at(static_cast<std::size_t>(c >> 4U)));
      encoded.push_back(kHex.at(static_cast<std::size_t>(c & 0x0FU)));
    }
  }
  return encoded;
}

std::string join_comma(const std::vector<std::string>& values) {
  std::string joined;
  for (const auto& value : values) {
    if (value.empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined.push_back(',');
    }
    joined += value;
  }
  return joined;
}

QueryBuilder& QueryBuilder::add(std::string_view key, std::string_view value) {
  query_.push_back(query_.empty() ? '?' : '&');
  query_ += percent_encode(key);
  query_.push_back('=');
  query_ += percent_encode(value);
  return *this;
}

QueryBuilder& QueryBuilder::add_bool(std::string_view key, bool value) {
  return add(key, value ? "true" : "false");
}

QueryBuilder& QueryBuilder::add_number(std::string_view key, std::uint64_t value) {
  return add(key, std::to_string(value));
}

QueryBuilder& QueryBuilder::add_if_not_empty(std::string_view key, std::string_view value) {
  if (!value.empty()) {
    add(key, value);
  }
  return *this;
}

std::string QueryBuilder::str() const {
  return query_;
}

}  // namespace radiobrowser::detail
