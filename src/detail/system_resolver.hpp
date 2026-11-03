// SPDX-License-Identifier: MIT
// Internal: blocking DNS lookups with the operating system resolver.

#pragma once

#include <radiobrowser/result.hpp>

#include <string>
#include <vector>

namespace radiobrowser::detail {

/// Resolves all addresses (IPv4 and IPv6) of `host` and returns the host
/// names obtained by reverse lookups of these addresses. Addresses without a
/// reverse record are skipped. Blocks; the duration is bounded only by the
/// system resolver's own timeouts.
[[nodiscard]] Result<std::vector<std::string>> resolve_reverse_hostnames(const std::string& host);

}  // namespace radiobrowser::detail
