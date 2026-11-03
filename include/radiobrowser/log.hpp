// SPDX-License-Identifier: MIT
/// @file
/// Logging hook. The library is silent unless a callback is installed.

#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace radiobrowser {

/// Severity of a log message.
enum class LogLevel : std::uint8_t {
  Debug,    ///< Detailed diagnostics such as every request URL.
  Info,     ///< Notable events such as a refreshed server list.
  Warning,  ///< Recoverable problems such as a failover to another server.
  Error,    ///< A call failed.
};

/// Receives log messages. Called synchronously from the thread that performs
/// the call; it may be called from several threads concurrently and must be
/// thread-safe. Exceptions thrown by the callback are swallowed.
using LogCallback = std::function<void(LogLevel level, std::string_view message)>;

/// Returns a lower case name for a log level.
[[nodiscard]] constexpr std::string_view to_string(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Warning:
      return "warning";
    case LogLevel::Error:
      return "error";
  }
  return "unknown";
}

}  // namespace radiobrowser
