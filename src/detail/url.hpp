// SPDX-License-Identifier: MIT
// Internal helpers to build request URLs.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace radiobrowser::detail {

/// Percent-encodes everything except RFC 3986 unreserved characters. Suitable
/// for path segments and query components alike.
[[nodiscard]] std::string percent_encode(std::string_view text);

/// Joins values with commas, dropping empty values.
[[nodiscard]] std::string join_comma(const std::vector<std::string>& values);

/// Collects query parameters and renders them as "?a=1&b=2".
class QueryBuilder {
 public:
  /// Adds a parameter; the key and value are percent-encoded.
  QueryBuilder& add(std::string_view key, std::string_view value);
  /// Adds a boolean as "true" or "false".
  QueryBuilder& add_bool(std::string_view key, bool value);
  /// Adds an unsigned number.
  QueryBuilder& add_number(std::string_view key, std::uint64_t value);
  /// Adds the parameter only if the value is not empty.
  QueryBuilder& add_if_not_empty(std::string_view key, std::string_view value);

  /// Returns "" if no parameter was added, otherwise "?k=v&...".
  [[nodiscard]] std::string str() const;

 private:
  std::string query_;
};

}  // namespace radiobrowser::detail
