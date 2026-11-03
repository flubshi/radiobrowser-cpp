// SPDX-License-Identifier: MIT
/// @file
/// Server discovery. The radio-browser API is served by a changing set of
/// mirrors; clients must look them up at runtime instead of hard-coding one.

#pragma once

#include <radiobrowser/cancellation.hpp>
#include <radiobrowser/export.hpp>
#include <radiobrowser/http.hpp>
#include <radiobrowser/result.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
// Private STL members of exported classes are not part of the ABI.
#pragma warning(disable : 4251)
#endif

namespace radiobrowser {

/// The default host name whose DNS records list all API servers.
inline constexpr const char* kDefaultDiscoveryHost = "all.api.radio-browser.info";
/// Only host names with this suffix are accepted as API servers.
inline constexpr const char* kDefaultServerSuffix = ".api.radio-browser.info";
/// The default bootstrap URL for HTTP based discovery.
inline constexpr const char* kDefaultBootstrapUrl = "https://all.api.radio-browser.info";

/// Context passed to ServerDiscovery::discover().
struct DiscoveryRequest {
  /// Discovery must return (with ErrorCode::Timeout) once this point is reached.
  std::chrono::steady_clock::time_point deadline;
  /// Discovery should return ErrorCode::Cancelled once this token is cancelled.
  CancellationToken cancellation;
  /// The client's transport. May be used for HTTP based discovery. Not owned.
  HttpTransport* transport = nullptr;
  /// The client's User-Agent; use it for HTTP requests.
  std::string user_agent;
  /// The client's response size limit.
  std::size_t max_response_bytes = 0;
};

/// Finds API servers.
///
/// Implementations must be thread-safe and must not throw. They return base
/// URLs without trailing slash, e.g. "https://de1.api.radio-browser.info".
class RADIOBROWSER_EXPORT ServerDiscovery {
 public:
  ServerDiscovery() = default;
  ServerDiscovery(const ServerDiscovery&) = delete;
  ServerDiscovery& operator=(const ServerDiscovery&) = delete;
  ServerDiscovery(ServerDiscovery&&) = delete;
  ServerDiscovery& operator=(ServerDiscovery&&) = delete;
  virtual ~ServerDiscovery();

  /// Returns the base URLs of the currently available servers.
  [[nodiscard]] virtual Result<std::vector<std::string>> discover(
      const DiscoveryRequest& request) = 0;
};

/// Always returns a fixed list of base URLs. Useful for tests, for a self
/// hosted mirror, or when the user picked a server explicitly.
class RADIOBROWSER_EXPORT StaticServerDiscovery final : public ServerDiscovery {
 public:
  /// @param base_urls Base URLs such as "https://de1.api.radio-browser.info".
  explicit StaticServerDiscovery(std::vector<std::string> base_urls);

  [[nodiscard]] Result<std::vector<std::string>> discover(const DiscoveryRequest& request) override;

 private:
  std::vector<std::string> base_urls_;
};

/// Options for DnsServerDiscovery.
struct DnsDiscoveryOptions {
  /// Resolves a host name to the host names of all its addresses (A/AAAA
  /// lookup followed by reverse lookups). May block. Leave empty to use the
  /// operating system resolver (getaddrinfo/getnameinfo).
  using Resolver = std::function<Result<std::vector<std::string>>(const std::string& host)>;

  /// The host name whose addresses are the API servers.
  std::string host = kDefaultDiscoveryHost;
  /// Only resolved host names ending with this suffix are used.
  std::string required_suffix = kDefaultServerSuffix;
  /// Maximum time one discover() call waits for the lookup, so that a
  /// fallback discovery still has time left when DNS is slow or blocked. A
  /// lookup that is still running is reused by the next call.
  std::chrono::milliseconds timeout{4000};
  /// Custom resolver, e.g. for tests. Empty means the system resolver.
  Resolver resolver;
};

/// Discovers servers through DNS as recommended by the API documentation:
/// resolves all addresses of "all.api.radio-browser.info" and maps them back
/// to host names through reverse DNS (needed for TLS certificate checks).
///
/// The system resolver cannot be interrupted, so lookups run on a background
/// thread owned by this object; discover() returns at the deadline even if the
/// lookup is still running. The destructor waits for a running lookup.
class RADIOBROWSER_EXPORT DnsServerDiscovery final : public ServerDiscovery {
 public:
  /// Creates the discovery. Lookups start lazily on the first discover() call.
  explicit DnsServerDiscovery(DnsDiscoveryOptions options = {});
  ~DnsServerDiscovery() override;

  [[nodiscard]] Result<std::vector<std::string>> discover(const DiscoveryRequest& request) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/// Options for HttpServerDiscovery.
struct HttpDiscoveryOptions {
  /// Base URLs that are asked for the server list (GET /json/servers), in order.
  std::vector<std::string> bootstrap_urls{kDefaultBootstrapUrl};
  /// Only host names ending with this suffix are used.
  std::string required_suffix = kDefaultServerSuffix;
};

/// Discovers servers by asking a bootstrap server for /json/servers through the
/// client's HttpTransport. Use it where DNS lookups are not possible, or where
/// all traffic must go through the configured transport (for example behind a
/// proxy or with an application provided network stack).
class RADIOBROWSER_EXPORT HttpServerDiscovery final : public ServerDiscovery {
 public:
  /// Creates the discovery.
  explicit HttpServerDiscovery(HttpDiscoveryOptions options = {});

  [[nodiscard]] Result<std::vector<std::string>> discover(const DiscoveryRequest& request) override;

 private:
  HttpDiscoveryOptions options_;
};

/// Tries several discoveries in order and returns the first non-empty result.
class RADIOBROWSER_EXPORT FallbackServerDiscovery final : public ServerDiscovery {
 public:
  /// @param discoveries Discoveries to try in order; null entries are skipped.
  explicit FallbackServerDiscovery(std::vector<std::shared_ptr<ServerDiscovery>> discoveries);

  [[nodiscard]] Result<std::vector<std::string>> discover(const DiscoveryRequest& request) override;

 private:
  std::vector<std::shared_ptr<ServerDiscovery>> discoveries_;
};

/// Returns the default discovery: DNS first, then HTTP via the client's
/// transport as a fallback.
[[nodiscard]] RADIOBROWSER_EXPORT std::shared_ptr<ServerDiscovery> make_default_discovery();

}  // namespace radiobrowser

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
