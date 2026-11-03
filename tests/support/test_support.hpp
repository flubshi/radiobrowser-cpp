// SPDX-License-Identifier: MIT
// Shared helpers for the unit tests.

#pragma once

#include <radiobrowser/radiobrowser.hpp>

#include <gmock/gmock.h>

#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace radiobrowser::test {

/// Reads a file from tests/fixtures.
inline std::string load_fixture(const std::string& name) {
  const std::string path = std::string(RADIOBROWSER_FIXTURE_DIR) + "/" + name;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot open fixture " + path);
  }
  std::ostringstream content;
  content << file.rdbuf();
  return content.str();
}

/// gMock transport for expectation based tests.
class MockTransport : public HttpTransport {
 public:
  MOCK_METHOD(Result<HttpResponse>, perform, (const HttpRequest& request), (override));
};

/// Thread-safe fake transport that records requests and delegates to a handler.
class FakeTransport : public HttpTransport {
 public:
  using Handler = std::function<Result<HttpResponse>(const HttpRequest&)>;

  explicit FakeTransport(Handler handler) : handler_(std::move(handler)) {}

  Result<HttpResponse> perform(const HttpRequest& request) override {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      requests_.push_back(request);
    }
    return handler_(request);
  }

  std::vector<HttpRequest> requests() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
  }

  std::vector<std::string> urls() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    result.reserve(requests_.size());
    for (const auto& request : requests_) {
      result.push_back(request.url);
    }
    return result;
  }

  std::size_t request_count() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return requests_.size();
  }

 private:
  Handler handler_;
  mutable std::mutex mutex_;
  std::vector<HttpRequest> requests_;
};

/// Discovery that returns a fixed list and counts its invocations.
class CountingDiscovery : public ServerDiscovery {
 public:
  explicit CountingDiscovery(std::vector<std::string> servers) : servers_(std::move(servers)) {}

  Result<std::vector<std::string>> discover(const DiscoveryRequest& /*request*/) override {
    const std::lock_guard<std::mutex> lock(mutex_);
    ++calls_;
    if (servers_.empty()) {
      return Error{ErrorCode::NoServers, "no servers configured", 0};
    }
    return servers_;
  }

  int calls() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return calls_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::string> servers_;
  int calls_ = 0;
};

inline HttpResponse ok_response(std::string body) {
  return HttpResponse{200, std::move(body)};
}

/// Returns the value of a header or an empty string.
inline std::string header_value(const HttpRequest& request, const std::string& name) {
  for (const auto& header : request.headers) {
    if (header.name == name) {
      return header.value;
    }
  }
  return {};
}

}  // namespace radiobrowser::test
