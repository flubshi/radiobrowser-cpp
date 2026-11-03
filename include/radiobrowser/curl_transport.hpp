// SPDX-License-Identifier: MIT
/// @file
/// HttpTransport implementation based on libcurl. Only available if the
/// library was built with RADIOBROWSER_WITH_CURL (see RADIOBROWSER_HAS_CURL).

#pragma once

#include <radiobrowser/config.hpp>
#include <radiobrowser/export.hpp>
#include <radiobrowser/http.hpp>
#include <radiobrowser/result.hpp>

#include <cstddef>
#include <memory>
#include <string>

#if !RADIOBROWSER_HAS_CURL
#error "radiobrowser was built without libcurl support (RADIOBROWSER_WITH_CURL=OFF)"
#endif

#if defined(_MSC_VER)
#pragma warning(push)
// Private STL members of exported classes are not part of the ABI.
#pragma warning(disable : 4251)
#endif

namespace radiobrowser {

/// Options for CurlTransport.
struct CurlTransportOptions {
  /// Path to a PEM bundle of trusted CA certificates. Empty means the default
  /// of the libcurl build. Needed on platforms without a system CA store
  /// libcurl knows about, e.g. Android.
  std::string ca_bundle_path;
  /// Proxy URL such as "http://proxy:3128". Empty means libcurl's default,
  /// which honours the usual proxy environment variables.
  std::string proxy;
  /// Follow HTTP redirects. A redirect never downgrades an HTTPS request to
  /// plain HTTP.
  bool follow_redirects = true;
  /// Maximum number of redirects to follow.
  long max_redirects = 5;  // NOLINT(google-runtime-int): libcurl uses long
  /// Number of idle libcurl handles kept for reuse. Reused handles keep their
  /// connections, DNS cache and TLS sessions, so consecutive calls skip the
  /// TCP and TLS handshakes. 0 disables reuse.
  std::size_t max_idle_handles = 4;
};

/// Performs HTTP requests with libcurl.
///
/// Thread-safe: concurrent requests use separate libcurl handles; idle handles
/// are pooled to reuse connections. TLS certificates are always verified.
/// Cancellation is checked by libcurl's progress callback, which runs at least
/// once per second.
class RADIOBROWSER_EXPORT CurlTransport final : public HttpTransport {
 public:
  /// Creates the transport. libcurl is initialised on first use.
  explicit CurlTransport(CurlTransportOptions options = {});
  /// Releases all pooled handles and their connections.
  ~CurlTransport() override;

  [[nodiscard]] Result<HttpResponse> perform(const HttpRequest& request) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace radiobrowser

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
