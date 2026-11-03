// SPDX-License-Identifier: MIT

#include "detail/hostname.hpp"

#include <algorithm>
#include <cstddef>

namespace radiobrowser::detail {

namespace {

[[nodiscard]] constexpr char to_lower_ascii(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr bool is_hostname_char(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

[[nodiscard]] bool starts_with_ci(std::string_view text, std::string_view prefix) noexcept {
  if (text.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (to_lower_ascii(text[i]) != to_lower_ascii(prefix[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<std::string> normalize_hostname(std::string_view host) {
  if (!host.empty() && host.back() == '.') {
    host.remove_suffix(1);
  }
  if (host.empty()) {
    return std::nullopt;
  }
  std::string normalized;
  normalized.reserve(host.size());
  for (const char c : host) {
    const char lower = to_lower_ascii(c);
    if (!is_hostname_char(lower)) {
      return std::nullopt;
    }
    normalized.push_back(lower);
  }
  if (normalized.front() == '.' || normalized.front() == '-') {
    return std::nullopt;
  }
  return normalized;
}

std::vector<std::string> hostnames_to_base_urls(const std::vector<std::string>& hosts,
                                                std::string_view required_suffix) {
  std::string suffix;
  for (const char c : required_suffix) {
    suffix.push_back(to_lower_ascii(c));
  }

  std::vector<std::string> base_urls;
  for (const auto& host : hosts) {
    const auto normalized = normalize_hostname(host);
    if (!normalized) {
      continue;
    }
    const std::string_view name = *normalized;
    if (!suffix.empty() && (name.size() <= suffix.size() || !name.ends_with(suffix))) {
      continue;
    }
    std::string base_url = "https://" + *normalized;
    if (std::find(base_urls.begin(), base_urls.end(), base_url) == base_urls.end()) {
      base_urls.push_back(std::move(base_url));
    }
  }
  return base_urls;
}

std::optional<std::string> normalize_base_url(std::string_view url) {
  while (!url.empty() && url.back() == '/') {
    url.remove_suffix(1);
  }
  std::size_t scheme_length = 0;
  if (starts_with_ci(url, "https://")) {
    scheme_length = 8;
  } else if (starts_with_ci(url, "http://")) {
    scheme_length = 7;
  } else {
    return std::nullopt;
  }
  const std::string_view authority = url.substr(scheme_length);
  if (authority.empty()) {
    return std::nullopt;
  }
  for (const char c : url) {
    // Reject whitespace, control characters and characters that would start
    // a query or fragment; the library appends paths itself.
    if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F || c == '?' || c == '#') {
      return std::nullopt;
    }
  }
  return std::string(url);
}

}  // namespace radiobrowser::detail
