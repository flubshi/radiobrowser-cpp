// SPDX-License-Identifier: MIT

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace radiobrowser {
namespace {

using test::CountingDiscovery;
using test::FakeTransport;
using test::header_value;
using test::load_fixture;
using test::ok_response;

DiscoveryRequest make_request(std::chrono::milliseconds budget = std::chrono::seconds(5),
                              HttpTransport* transport = nullptr) {
  DiscoveryRequest request;
  request.deadline = std::chrono::steady_clock::now() + budget;
  request.transport = transport;
  request.user_agent = "discovery-test/1.0";
  request.max_response_bytes = 1024 * 1024;
  return request;
}

// --- StaticServerDiscovery -----------------------------------------------------

TEST(StaticDiscoveryTest, NormalizesAndFiltersUrls) {
  StaticServerDiscovery discovery({"https://de1.api.radio-browser.info/", "not a url",
                                   "http://127.0.0.1:8080", "https://de1.api.radio-browser.info"});
  const auto servers = discovery.discover(make_request());
  ASSERT_TRUE(servers);
  EXPECT_EQ(*servers, (std::vector<std::string>{"https://de1.api.radio-browser.info",
                                                "http://127.0.0.1:8080"}));
}

TEST(StaticDiscoveryTest, FailsWithoutValidUrl) {
  StaticServerDiscovery discovery({"", "ftp://x"});
  const auto servers = discovery.discover(make_request());
  ASSERT_FALSE(servers);
  EXPECT_EQ(servers.error().code, ErrorCode::InvalidArgument);
}

// --- HttpServerDiscovery -------------------------------------------------------

TEST(HttpDiscoveryTest, ReadsServerListThroughTransport) {
  FakeTransport transport([](const HttpRequest&) {
    return Result<HttpResponse>(ok_response(load_fixture("servers.json")));
  });
  HttpServerDiscovery discovery;
  const auto servers = discovery.discover(make_request(std::chrono::seconds(5), &transport));
  ASSERT_TRUE(servers) << to_string(servers.error());
  EXPECT_EQ(*servers, (std::vector<std::string>{"https://de1.api.radio-browser.info",
                                                "https://de2.api.radio-browser.info",
                                                "https://fi1.api.radio-browser.info"}));

  const auto requests = transport.requests();
  ASSERT_EQ(requests.size(), 1U);
  EXPECT_EQ(requests.front().url, "https://all.api.radio-browser.info/json/servers");
  EXPECT_EQ(header_value(requests.front(), "User-Agent"), "discovery-test/1.0");
  EXPECT_EQ(requests.front().max_response_bytes, 1024U * 1024U);
}

TEST(HttpDiscoveryTest, TriesNextBootstrapOnFailure) {
  FakeTransport transport([](const HttpRequest& request) {
    if (request.url.starts_with("https://first.test")) {
      return Result<HttpResponse>(Error{ErrorCode::Network, "down", 0});
    }
    if (request.url.starts_with("https://second.test")) {
      return Result<HttpResponse>(HttpResponse{500, "error"});
    }
    if (request.url.starts_with("https://third.test")) {
      return Result<HttpResponse>(ok_response("[{\"ip\":\"1.1.1.1\",\"name\":\"evil.test\"}]"));
    }
    return Result<HttpResponse>(ok_response(load_fixture("servers.json")));
  });
  HttpDiscoveryOptions options;
  options.bootstrap_urls = {"https://first.test", "https://second.test", "https://third.test",
                            "https://fourth.test/"};
  HttpServerDiscovery discovery(options);
  const auto servers = discovery.discover(make_request(std::chrono::seconds(5), &transport));
  ASSERT_TRUE(servers);
  EXPECT_EQ(servers->size(), 3U);
  EXPECT_EQ(transport.request_count(), 4U);
  EXPECT_EQ(transport.requests().back().url, "https://fourth.test/json/servers");
}

TEST(HttpDiscoveryTest, ReportsAllFailures) {
  FakeTransport transport([](const HttpRequest&) {
    return Result<HttpResponse>(Error{ErrorCode::Network, "down", 0});
  });
  HttpDiscoveryOptions options;
  options.bootstrap_urls = {"https://first.test", "https://second.test"};
  HttpServerDiscovery discovery(options);
  const auto servers = discovery.discover(make_request(std::chrono::seconds(5), &transport));
  ASSERT_FALSE(servers);
  EXPECT_EQ(servers.error().code, ErrorCode::NoServers);
  EXPECT_NE(servers.error().message.find("first.test"), std::string::npos);
  EXPECT_NE(servers.error().message.find("second.test"), std::string::npos);
}

TEST(HttpDiscoveryTest, RequiresTransport) {
  HttpServerDiscovery discovery;
  const auto servers = discovery.discover(make_request());
  ASSERT_FALSE(servers);
  EXPECT_EQ(servers.error().code, ErrorCode::InvalidArgument);
}

TEST(HttpDiscoveryTest, StopsWhenCancelled) {
  FakeTransport transport(
      [](const HttpRequest&) { return Result<HttpResponse>(ok_response("[]")); });
  CancellationSource source;
  source.cancel();
  DiscoveryRequest request = make_request(std::chrono::seconds(5), &transport);
  request.cancellation = source.token();
  HttpServerDiscovery discovery;
  const auto servers = discovery.discover(request);
  ASSERT_FALSE(servers);
  EXPECT_EQ(servers.error().code, ErrorCode::Cancelled);
  EXPECT_EQ(transport.request_count(), 0U);
}

// --- DnsServerDiscovery --------------------------------------------------------

TEST(DnsDiscoveryTest, MapsResolvedNamesToBaseUrls) {
  DnsDiscoveryOptions options;
  options.resolver = [](const std::string& host) -> Result<std::vector<std::string>> {
    EXPECT_EQ(host, "all.api.radio-browser.info");
    return std::vector<std::string>{"de1.api.radio-browser.info.", "fi1.api.radio-browser.info",
                                    "static.hoster.example"};
  };
  DnsServerDiscovery discovery(options);
  const auto servers = discovery.discover(make_request());
  ASSERT_TRUE(servers) << to_string(servers.error());
  EXPECT_EQ(*servers, (std::vector<std::string>{"https://de1.api.radio-browser.info",
                                                "https://fi1.api.radio-browser.info"}));
}

TEST(DnsDiscoveryTest, PropagatesResolverErrorsAndExceptions) {
  DnsDiscoveryOptions failing;
  failing.resolver = [](const std::string&) -> Result<std::vector<std::string>> {
    return Error{ErrorCode::Network, "NXDOMAIN", 0};
  };
  DnsServerDiscovery failing_discovery(failing);
  const auto failed = failing_discovery.discover(make_request());
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().code, ErrorCode::Network);

  DnsDiscoveryOptions throwing;
  throwing.resolver = [](const std::string&) -> Result<std::vector<std::string>> {
    throw std::runtime_error("resolver bug");
  };
  DnsServerDiscovery throwing_discovery(throwing);
  const auto thrown = throwing_discovery.discover(make_request());
  ASSERT_FALSE(thrown);
  EXPECT_EQ(thrown.error().code, ErrorCode::Internal);

  DnsDiscoveryOptions unusable;
  unusable.resolver = [](const std::string&) -> Result<std::vector<std::string>> {
    return std::vector<std::string>{"cdn.example.net"};
  };
  DnsServerDiscovery unusable_discovery(unusable);
  const auto empty = unusable_discovery.discover(make_request());
  ASSERT_FALSE(empty);
  EXPECT_EQ(empty.error().code, ErrorCode::NoServers);
}

TEST(DnsDiscoveryTest, ReturnsAtTimeoutAndReusesRunningLookup) {
  std::atomic<int> calls{0};
  DnsDiscoveryOptions options;
  options.timeout = std::chrono::milliseconds(50);
  options.resolver = [&calls](const std::string&) -> Result<std::vector<std::string>> {
    ++calls;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    return std::vector<std::string>{"de1.api.radio-browser.info"};
  };
  {
    DnsServerDiscovery discovery(options);

    const auto started = std::chrono::steady_clock::now();
    const auto first = discovery.discover(make_request());
    const auto elapsed = std::chrono::steady_clock::now() - started;
    ASSERT_FALSE(first);
    EXPECT_EQ(first.error().code, ErrorCode::Timeout);
    EXPECT_LT(elapsed, std::chrono::milliseconds(350));

    // The second call joins the lookup that is still running.
    DiscoveryRequest patient = make_request();
    std::vector<std::string> servers;
    for (int attempt = 0; attempt < 40 && servers.empty(); ++attempt) {
      const auto result = discovery.discover(patient);
      if (result) {
        servers = *result;
      }
    }
    EXPECT_EQ(servers, (std::vector<std::string>{"https://de1.api.radio-browser.info"}));
    EXPECT_EQ(calls.load(), 1);
  }  // The destructor joins the worker thread.
}

TEST(DnsDiscoveryTest, DestructorWaitsForRunningLookup) {
  std::atomic<bool> finished{false};
  {
    DnsDiscoveryOptions options;
    options.timeout = std::chrono::milliseconds(10);
    options.resolver = [&finished](const std::string&) -> Result<std::vector<std::string>> {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      finished = true;
      return std::vector<std::string>{};
    };
    DnsServerDiscovery discovery(options);
    static_cast<void>(discovery.discover(make_request()));
  }
  EXPECT_TRUE(finished.load());
}

TEST(DnsDiscoveryTest, StopsWaitingWhenCancelled) {
  DnsDiscoveryOptions options;
  options.timeout = std::chrono::seconds(5);
  options.resolver = [](const std::string&) -> Result<std::vector<std::string>> {
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    return std::vector<std::string>{};
  };
  DnsServerDiscovery discovery(options);
  CancellationSource source;
  DiscoveryRequest request = make_request();
  request.cancellation = source.token();
  std::thread canceller([&source] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    source.cancel();
  });
  const auto started = std::chrono::steady_clock::now();
  const auto result = discovery.discover(request);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  canceller.join();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
  EXPECT_LT(elapsed, std::chrono::milliseconds(1000));
}

// --- FallbackServerDiscovery ---------------------------------------------------

class FailingDiscovery : public ServerDiscovery {
 public:
  explicit FailingDiscovery(ErrorCode code) : code_(code) {}
  Result<std::vector<std::string>> discover(const DiscoveryRequest&) override {
    ++calls;
    return Error{code_, "failing discovery", 0};
  }
  int calls = 0;

 private:
  ErrorCode code_;
};

TEST(FallbackDiscoveryTest, UsesFirstSuccessfulDiscovery) {
  auto failing = std::make_shared<FailingDiscovery>(ErrorCode::Network);
  auto empty = std::make_shared<CountingDiscovery>(std::vector<std::string>{});
  auto working = std::make_shared<CountingDiscovery>(
      std::vector<std::string>{"https://de1.api.radio-browser.info"});
  auto never = std::make_shared<CountingDiscovery>(
      std::vector<std::string>{"https://never.api.radio-browser.info"});
  FallbackServerDiscovery discovery({failing, nullptr, empty, working, never});

  const auto servers = discovery.discover(make_request());
  ASSERT_TRUE(servers);
  EXPECT_EQ(servers->front(), "https://de1.api.radio-browser.info");
  EXPECT_EQ(failing->calls, 1);
  EXPECT_EQ(empty->calls(), 1);
  EXPECT_EQ(never->calls(), 0);
}

TEST(FallbackDiscoveryTest, AggregatesFailures) {
  FallbackServerDiscovery discovery({std::make_shared<FailingDiscovery>(ErrorCode::Network),
                                     std::make_shared<FailingDiscovery>(ErrorCode::Timeout)});
  const auto servers = discovery.discover(make_request());
  ASSERT_FALSE(servers);
  EXPECT_EQ(servers.error().code, ErrorCode::NoServers);
  EXPECT_NE(servers.error().message.find("network error"), std::string::npos);
  EXPECT_NE(servers.error().message.find("timeout"), std::string::npos);
}

TEST(FallbackDiscoveryTest, StopsOnCancellation) {
  auto cancelled = std::make_shared<FailingDiscovery>(ErrorCode::Cancelled);
  auto next = std::make_shared<FailingDiscovery>(ErrorCode::Network);
  FallbackServerDiscovery discovery({cancelled, next});
  const auto servers = discovery.discover(make_request());
  ASSERT_FALSE(servers);
  EXPECT_EQ(servers.error().code, ErrorCode::Cancelled);
  EXPECT_EQ(next->calls, 0);
}

TEST(FallbackDiscoveryTest, DefaultDiscoveryFallsBackToHttp) {
  // Use the building blocks of the default discovery with a failing resolver
  // and verify the HTTP fallback through the transport is used.
  DnsDiscoveryOptions dns;
  dns.resolver = [](const std::string&) -> Result<std::vector<std::string>> {
    return Error{ErrorCode::Network, "DNS blocked", 0};
  };
  FakeTransport transport([](const HttpRequest&) {
    return Result<HttpResponse>(ok_response(load_fixture("servers.json")));
  });
  FallbackServerDiscovery discovery(
      {std::make_shared<DnsServerDiscovery>(dns), std::make_shared<HttpServerDiscovery>()});
  const auto servers = discovery.discover(make_request(std::chrono::seconds(5), &transport));
  ASSERT_TRUE(servers);
  EXPECT_EQ(servers->size(), 3U);
  EXPECT_NE(make_default_discovery(), nullptr);
}

}  // namespace
}  // namespace radiobrowser
