// SPDX-License-Identifier: MIT
// Internal helpers to validate host names and base URLs.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace radiobrowser::detail {

/// Lower-cases a DNS host name and removes a trailing dot. Returns
/// std::nullopt if it is empty or contains characters outside [a-z0-9.-].
[[nodiscard]] std::optional<std::string> normalize_hostname(std::string_view host);

/// Turns host names into "https://<host>" base URLs. Only names that end with
/// required_suffix (and are longer than it) are kept; an empty suffix accepts
/// every valid name. Duplicates are removed, the first occurrence wins.
[[nodiscard]] std::vector<std::string> hostnames_to_base_urls(const std::vector<std::string>& hosts,
                                                              std::string_view required_suffix);

/// Validates a base URL ("http://" or "https://" followed by an authority)
/// and removes trailing slashes. Returns std::nullopt if it is invalid.
[[nodiscard]] std::optional<std::string> normalize_base_url(std::string_view url);

}  // namespace radiobrowser::detail
