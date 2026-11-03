// SPDX-License-Identifier: MIT
// Internal helpers shared by the client and the discoveries.

#pragma once

#include <radiobrowser/discovery.hpp>
#include <radiobrowser/http.hpp>
#include <radiobrowser/result.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <vector>

namespace radiobrowser::detail {

using SteadyClock = std::chrono::steady_clock;

/// Time left until the deadline, never negative.
[[nodiscard]] inline std::chrono::milliseconds remaining_until(SteadyClock::time_point deadline) {
  const auto left =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - SteadyClock::now());
  return std::max(left, std::chrono::milliseconds::zero());
}

/// Calls the transport and converts exceptions into ErrorCode::Internal.
[[nodiscard]] inline Result<HttpResponse> perform_safely(HttpTransport& transport,
                                                         const HttpRequest& request) {
  try {
    return transport.perform(request);
  } catch (const std::exception& e) {
    return Error{ErrorCode::Internal, std::string("HTTP transport threw: ") + e.what(), 0};
  } catch (...) {
    return Error{ErrorCode::Internal, "HTTP transport threw an unknown exception", 0};
  }
}

/// Calls the discovery and converts exceptions into ErrorCode::Internal.
[[nodiscard]] inline Result<std::vector<std::string>> discover_safely(
    ServerDiscovery& discovery, const DiscoveryRequest& request) {
  try {
    return discovery.discover(request);
  } catch (const std::exception& e) {
    return Error{ErrorCode::Internal, std::string("server discovery threw: ") + e.what(), 0};
  } catch (...) {
    return Error{ErrorCode::Internal, "server discovery threw an unknown exception", 0};
  }
}

/// True for 2xx status codes.
[[nodiscard]] constexpr bool is_success_status(int status) noexcept {
  return status >= 200 && status < 300;
}

/// Joins messages with "; ".
[[nodiscard]] inline std::string join_messages(const std::vector<std::string>& messages) {
  std::string joined;
  for (const auto& message : messages) {
    if (!joined.empty()) {
      joined += "; ";
    }
    joined += message;
  }
  return joined;
}

}  // namespace radiobrowser::detail
