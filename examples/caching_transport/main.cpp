// SPDX-License-Identifier: MIT
// caching-transport: shows how to extend the library with a custom
// HttpTransport. CachingTransport decorates another transport and serves
// repeated GET requests from memory for a configurable time, which suits data
// that rarely changes, such as the lists of countries, languages and tags.
//
//   radiobrowser-caching-transport

#include <radiobrowser/radiobrowser.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

namespace rb = radiobrowser;

/// Caches successful GET responses. Thread-safe like every transport must be.
class CachingTransport final : public rb::HttpTransport {
 public:
  CachingTransport(std::shared_ptr<rb::HttpTransport> inner, std::chrono::seconds ttl,
                   std::size_t max_entries)
      : inner_(std::move(inner)), ttl_(ttl), max_entries_(max_entries) {}

  rb::Result<rb::HttpResponse> perform(const rb::HttpRequest& request) override {
    if (!is_cacheable(request)) {
      return inner_->perform(request);
    }
    // The client fails over between servers, so the key ignores the host.
    const std::string key = path_and_query(request.url);
    const auto now = Clock::now();
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      const auto it = entries_.find(key);
      if (it != entries_.end() && it->second.expires > now) {
        ++hits_;
        return it->second.response;
      }
    }

    auto response = inner_->perform(request);
    if (response && response->status == 200) {
      const std::lock_guard<std::mutex> lock(mutex_);
      if (entries_.size() >= max_entries_ && entries_.find(key) == entries_.end()) {
        evict_one(now);
      }
      entries_[key] = Entry{*response, now + ttl_};
    }
    return response;
  }

  [[nodiscard]] std::size_t hits() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return hits_;
  }

 private:
  using Clock = std::chrono::steady_clock;

  struct Entry {
    rb::HttpResponse response;
    Clock::time_point expires;
  };

  // Clicks and votes are actions and must always reach the server.
  static bool is_cacheable(const rb::HttpRequest& request) {
    return request.method == rb::HttpMethod::Get &&
           request.url.find("/json/url/") == std::string::npos &&
           request.url.find("/json/vote/") == std::string::npos;
  }

  static std::string path_and_query(const std::string& url) {
    const auto scheme_end = url.find("://");
    const auto path_start = url.find('/', scheme_end == std::string::npos ? 0 : scheme_end + 3);
    return path_start == std::string::npos ? std::string("/") : url.substr(path_start);
  }

  // Requires mutex_. Removes an expired entry, otherwise the oldest one.
  void evict_one(Clock::time_point now) {
    if (entries_.empty()) {
      return;
    }
    auto victim = std::min_element(
        entries_.begin(), entries_.end(),
        [](const auto& a, const auto& b) { return a.second.expires < b.second.expires; });
    const auto expired = std::find_if(entries_.begin(), entries_.end(),
                                      [now](const auto& e) { return e.second.expires <= now; });
    if (expired != entries_.end()) {
      victim = expired;
    }
    entries_.erase(victim);
  }

  std::shared_ptr<rb::HttpTransport> inner_;
  std::chrono::seconds ttl_;
  std::size_t max_entries_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> entries_;
  std::size_t hits_ = 0;
};

int run() {
  auto cache = std::make_shared<CachingTransport>(std::make_shared<rb::CurlTransport>(),
                                                  std::chrono::minutes(10), 256);

  rb::ClientOptions options;
  options.user_agent = std::string("radiobrowser-caching-transport/") + rb::kVersion;
  options.transport = cache;
  auto created = rb::Client::create(std::move(options));
  if (!created) {
    std::cerr << "error: " << rb::to_string(created.error()) << '\n';
    return EXIT_FAILURE;
  }
  const rb::Client& client = *created;

  for (int round = 1; round <= 2; ++round) {
    const auto started = std::chrono::steady_clock::now();
    const auto countries = client.countries();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (!countries) {
      std::cerr << "error: " << rb::to_string(countries.error()) << '\n';
      return EXIT_FAILURE;
    }
    std::cout << "round " << round << ": " << countries->size() << " countries in "
              << elapsed.count() << " ms\n";
  }
  std::cout << "served from cache: " << cache->hits() << " request(s)\n";
  return EXIT_SUCCESS;
}

}  // namespace

int main() {
  try {
    return run();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
