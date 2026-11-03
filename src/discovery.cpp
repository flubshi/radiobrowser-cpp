// SPDX-License-Identifier: MIT

#include <radiobrowser/discovery.hpp>

#include "detail/call_helpers.hpp"
#include "detail/hostname.hpp"
#include "detail/json_mapping.hpp"
#include "detail/system_resolver.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace radiobrowser {

namespace {

using detail::SteadyClock;

constexpr std::chrono::milliseconds kPollInterval{50};
constexpr std::chrono::milliseconds kDiscoveryConnectTimeout{5000};

[[nodiscard]] Error cancelled_error() {
  return Error{ErrorCode::Cancelled, "server discovery was cancelled", 0};
}

[[nodiscard]] Result<std::vector<std::string>> run_resolver(
    const DnsDiscoveryOptions::Resolver& resolver, const std::string& host) {
  try {
    return resolver(host);
  } catch (const std::exception& e) {
    return Error{ErrorCode::Internal, std::string("DNS resolver threw: ") + e.what(), 0};
  } catch (...) {
    return Error{ErrorCode::Internal, "DNS resolver threw an unknown exception", 0};
  }
}

}  // namespace

ServerDiscovery::~ServerDiscovery() = default;

// --- StaticServerDiscovery ---------------------------------------------------

StaticServerDiscovery::StaticServerDiscovery(std::vector<std::string> base_urls)
    : base_urls_(std::move(base_urls)) {}

Result<std::vector<std::string>> StaticServerDiscovery::discover(
    const DiscoveryRequest& /*request*/) {
  std::vector<std::string> normalized;
  for (const auto& url : base_urls_) {
    if (auto base = detail::normalize_base_url(url)) {
      if (std::find(normalized.begin(), normalized.end(), *base) == normalized.end()) {
        normalized.push_back(std::move(*base));
      }
    }
  }
  if (normalized.empty()) {
    return Error{ErrorCode::InvalidArgument, "no valid base URL configured", 0};
  }
  return normalized;
}

// --- DnsServerDiscovery ------------------------------------------------------

class DnsServerDiscovery::Impl {
 public:
  explicit Impl(DnsDiscoveryOptions options) : options_(std::move(options)) {
    if (!options_.resolver) {
      options_.resolver = detail::resolve_reverse_hostnames;
    }
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  ~Impl() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  Result<std::vector<std::string>> discover(const DiscoveryRequest& request) {
    if (request.cancellation.is_cancelled()) {
      return cancelled_error();
    }
    const auto wait_until = std::min(request.deadline, SteadyClock::now() + options_.timeout);

    std::shared_ptr<Lookup> lookup;
    try {
      lookup = start_or_join_lookup();
    } catch (const std::exception& e) {
      return Error{ErrorCode::Internal, std::string("failed to start DNS lookup: ") + e.what(), 0};
    }

    std::unique_lock<std::mutex> lock(lookup->mutex);
    while (!lookup->done) {
      if (request.cancellation.is_cancelled()) {
        return cancelled_error();
      }
      const auto now = SteadyClock::now();
      if (now >= wait_until) {
        return Error{ErrorCode::Timeout,
                     "DNS lookup of " + options_.host + " did not finish in time", 0};
      }
      lookup->done_changed.wait_until(lock, std::min(wait_until, now + kPollInterval));
    }
    const std::optional<Result<std::vector<std::string>>>& stored = lookup->result;
    if (!stored.has_value()) {
      return Error{ErrorCode::Internal, "DNS lookup finished without a result", 0};
    }
    Result<std::vector<std::string>> hosts = stored.value();
    lock.unlock();

    if (!hosts) {
      return hosts.error();
    }
    auto base_urls = detail::hostnames_to_base_urls(*hosts, options_.required_suffix);
    if (base_urls.empty()) {
      return Error{ErrorCode::NoServers,
                   "DNS lookup of " + options_.host + " returned no host ending with '" +
                       options_.required_suffix + "'",
                   0};
    }
    return base_urls;
  }

 private:
  struct Lookup {
    std::mutex mutex;
    std::condition_variable done_changed;
    bool done = false;
    std::optional<Result<std::vector<std::string>>> result;
  };

  // Returns the running lookup or starts a new one. A lookup that is still
  // running when a call times out is reused by the next call.
  std::shared_ptr<Lookup> start_or_join_lookup() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (pending_) {
      const std::lock_guard<std::mutex> pending_lock(pending_->mutex);
      if (!pending_->done) {
        return pending_;
      }
    }
    if (worker_.joinable()) {
      worker_.join();  // Finished already; joining does not block.
    }
    auto lookup = std::make_shared<Lookup>();
    worker_ = std::thread([lookup, resolver = options_.resolver, host = options_.host] {
      auto result = run_resolver(resolver, host);
      {
        const std::lock_guard<std::mutex> result_lock(lookup->mutex);
        lookup->result.emplace(std::move(result));
        lookup->done = true;
      }
      lookup->done_changed.notify_all();
    });
    pending_ = lookup;
    return lookup;
  }

  DnsDiscoveryOptions options_;
  std::mutex mutex_;
  std::thread worker_;
  std::shared_ptr<Lookup> pending_;
};

DnsServerDiscovery::DnsServerDiscovery(DnsDiscoveryOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

DnsServerDiscovery::~DnsServerDiscovery() = default;

Result<std::vector<std::string>> DnsServerDiscovery::discover(const DiscoveryRequest& request) {
  return impl_->discover(request);
}

// --- HttpServerDiscovery -----------------------------------------------------

HttpServerDiscovery::HttpServerDiscovery(HttpDiscoveryOptions options)
    : options_(std::move(options)) {}

Result<std::vector<std::string>> HttpServerDiscovery::discover(const DiscoveryRequest& request) {
  if (request.transport == nullptr) {
    return Error{ErrorCode::InvalidArgument, "HTTP discovery requires a transport", 0};
  }
  std::vector<std::string> failures;
  for (const auto& bootstrap : options_.bootstrap_urls) {
    if (request.cancellation.is_cancelled()) {
      return cancelled_error();
    }
    const auto remaining = detail::remaining_until(request.deadline);
    if (remaining <= std::chrono::milliseconds::zero()) {
      failures.emplace_back("deadline expired");
      return Error{ErrorCode::Timeout, "HTTP discovery: " + detail::join_messages(failures), 0};
    }
    const auto normalized = detail::normalize_base_url(bootstrap);
    if (!normalized) {
      failures.push_back("invalid bootstrap URL '" + bootstrap + "'");
      continue;
    }
    const std::string& base_url = *normalized;

    HttpRequest http;
    http.url = base_url + "/json/servers";
    http.headers = {{"User-Agent", request.user_agent}, {"Accept", "application/json"}};
    http.timeout = remaining;
    http.connect_timeout = std::min(remaining, kDiscoveryConnectTimeout);
    http.max_response_bytes = request.max_response_bytes;
    http.cancellation = request.cancellation;

    auto response = detail::perform_safely(*request.transport, http);
    if (!response) {
      if (response.error().code == ErrorCode::Cancelled) {
        return std::move(response).error();
      }
      failures.push_back(base_url + ": " + to_string(response.error()));
      continue;
    }
    if (!detail::is_success_status(response->status)) {
      failures.push_back(base_url + ": HTTP status " + std::to_string(response->status));
      continue;
    }
    auto mirrors = detail::parse_server_mirrors(response->body);
    if (!mirrors) {
      failures.push_back(base_url + ": " + to_string(mirrors.error()));
      continue;
    }
    std::vector<std::string> names;
    names.reserve(mirrors->size());
    for (const auto& mirror : *mirrors) {
      names.push_back(mirror.name);
    }
    auto base_urls = detail::hostnames_to_base_urls(names, options_.required_suffix);
    if (base_urls.empty()) {
      failures.push_back(base_url + ": server list contains no usable host");
      continue;
    }
    return base_urls;
  }
  if (failures.empty()) {
    failures.emplace_back("no bootstrap URL configured");
  }
  return Error{ErrorCode::NoServers, "HTTP discovery: " + detail::join_messages(failures), 0};
}

// --- FallbackServerDiscovery -------------------------------------------------

FallbackServerDiscovery::FallbackServerDiscovery(
    std::vector<std::shared_ptr<ServerDiscovery>> discoveries)
    : discoveries_(std::move(discoveries)) {}

Result<std::vector<std::string>> FallbackServerDiscovery::discover(
    const DiscoveryRequest& request) {
  std::vector<std::string> failures;
  for (const auto& discovery : discoveries_) {
    if (!discovery) {
      continue;
    }
    if (request.cancellation.is_cancelled()) {
      return cancelled_error();
    }
    if (SteadyClock::now() >= request.deadline) {
      failures.emplace_back("deadline expired");
      return Error{ErrorCode::Timeout, detail::join_messages(failures), 0};
    }
    auto result = detail::discover_safely(*discovery, request);
    if (result && !result->empty()) {
      return result;
    }
    if (!result) {
      if (result.error().code == ErrorCode::Cancelled) {
        return result;
      }
      failures.push_back(to_string(result.error()));
    } else {
      failures.emplace_back("discovery returned an empty server list");
    }
  }
  if (failures.empty()) {
    failures.emplace_back("no discovery configured");
  }
  return Error{ErrorCode::NoServers, detail::join_messages(failures), 0};
}

std::shared_ptr<ServerDiscovery> make_default_discovery() {
  std::vector<std::shared_ptr<ServerDiscovery>> discoveries{
      std::make_shared<DnsServerDiscovery>(), std::make_shared<HttpServerDiscovery>()};
  return std::make_shared<FallbackServerDiscovery>(std::move(discoveries));
}

}  // namespace radiobrowser
