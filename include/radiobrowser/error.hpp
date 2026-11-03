// SPDX-License-Identifier: MIT
/// @file
/// Error codes and the Error type returned by every fallible operation.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace radiobrowser {

/// Categorises why an operation failed.
enum class ErrorCode : std::uint8_t {
  InvalidArgument,   ///< A parameter supplied by the caller is invalid.
  Network,           ///< Connection, DNS or TLS failure.
  Timeout,           ///< The deadline of the call expired.
  Cancelled,         ///< The call was cancelled through a CancellationToken.
  HttpStatus,        ///< The server answered with a non-success HTTP status.
  InvalidResponse,   ///< The response body could not be parsed.
  ResponseTooLarge,  ///< The response exceeded the configured size limit.
  NotFound,          ///< The requested entity does not exist.
  NoServers,         ///< Server discovery did not yield any usable server.
  AllServersFailed,  ///< Every known server failed to answer the request.
  Internal,          ///< Unexpected internal failure, e.g. an exception thrown by a callback.
};

/// Returns a stable, human readable name for an error code.
[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::InvalidArgument:
      return "invalid argument";
    case ErrorCode::Network:
      return "network error";
    case ErrorCode::Timeout:
      return "timeout";
    case ErrorCode::Cancelled:
      return "cancelled";
    case ErrorCode::HttpStatus:
      return "http status error";
    case ErrorCode::InvalidResponse:
      return "invalid response";
    case ErrorCode::ResponseTooLarge:
      return "response too large";
    case ErrorCode::NotFound:
      return "not found";
    case ErrorCode::NoServers:
      return "no servers";
    case ErrorCode::AllServersFailed:
      return "all servers failed";
    case ErrorCode::Internal:
      return "internal error";
  }
  return "unknown error";
}

/// Describes a failed operation.
struct Error {
  /// The category of the failure.
  ErrorCode code = ErrorCode::Internal;
  /// Additional, human readable details. Not meant to be parsed.
  std::string message;
  /// The HTTP status code if code is ErrorCode::HttpStatus, otherwise 0.
  int http_status = 0;
};

/// Formats an error as "<category>: <message>" for logging purposes.
[[nodiscard]] inline std::string to_string(const Error& error) {
  std::string text(to_string(error.code));
  if (!error.message.empty()) {
    text += ": ";
    text += error.message;
  }
  return text;
}

}  // namespace radiobrowser
