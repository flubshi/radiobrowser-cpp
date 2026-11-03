// SPDX-License-Identifier: MIT
/// @file
/// The HTTP abstraction. Implement HttpTransport to route all traffic through
/// the network stack of the host application; see docs/custom-transport.md.

#pragma once

#include <radiobrowser/cancellation.hpp>
#include <radiobrowser/export.hpp>
#include <radiobrowser/result.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace radiobrowser {

/// The HTTP method of a request.
enum class HttpMethod : std::uint8_t {
  Get,   ///< HTTP GET.
  Post,  ///< HTTP POST.
};

/// A single HTTP header.
struct HttpHeader {
  /// Header name, e.g. "User-Agent".
  std::string name;
  /// Header value.
  std::string value;
};

/// Everything a transport needs to perform one HTTP request.
struct HttpRequest {
  /// The request method. The library currently only issues GET requests.
  HttpMethod method = HttpMethod::Get;
  /// The absolute URL including the query string.
  std::string url;
  /// Request headers. Always contains "User-Agent" and "Accept".
  std::vector<HttpHeader> headers;
  /// Request body for POST requests.
  std::string body;
  /// Upper bound for the complete transfer. Transports must give up once it
  /// has elapsed and report ErrorCode::Timeout.
  std::chrono::milliseconds timeout{10000};
  /// Upper bound for establishing the connection.
  std::chrono::milliseconds connect_timeout{5000};
  /// Maximum accepted size of the response body in bytes; 0 means unlimited.
  /// Transports must abort larger transfers with ErrorCode::ResponseTooLarge.
  std::size_t max_response_bytes = 0;
  /// Transports should poll this token and abort with ErrorCode::Cancelled.
  CancellationToken cancellation;
};

/// The response to an HttpRequest.
struct HttpResponse {
  /// The HTTP status code, e.g. 200.
  int status = 0;
  /// The response body after content decoding.
  std::string body;
};

/// Performs HTTP requests on behalf of the library.
///
/// Requirements for implementations:
/// - perform() is called concurrently from several threads and must be
///   thread-safe.
/// - perform() must not throw. Exceptions are caught and reported as
///   ErrorCode::Internal, but transports should map failures to Error values:
///   ErrorCode::Network for connection, DNS and TLS failures,
///   ErrorCode::Timeout, ErrorCode::Cancelled and ErrorCode::ResponseTooLarge.
/// - Any HTTP status is a successful transfer; return it in HttpResponse.
/// - TLS certificates must be verified.
class RADIOBROWSER_EXPORT HttpTransport {
 public:
  HttpTransport() = default;
  HttpTransport(const HttpTransport&) = delete;
  HttpTransport& operator=(const HttpTransport&) = delete;
  HttpTransport(HttpTransport&&) = delete;
  HttpTransport& operator=(HttpTransport&&) = delete;
  virtual ~HttpTransport();

  /// Performs the request and returns the response or a transport error.
  [[nodiscard]] virtual Result<HttpResponse> perform(const HttpRequest& request) = 0;
};

}  // namespace radiobrowser
