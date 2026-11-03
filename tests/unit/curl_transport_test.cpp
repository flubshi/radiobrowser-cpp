// SPDX-License-Identifier: MIT
// Tests the libcurl transport and the complete client stack against a local
// HTTP server running inside the test process. No internet access is needed.

#include "test_support.hpp"

#include <radiobrowser/curl_transport.hpp>

#include <httplib.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace radiobrowser {
namespace {

using test::load_fixture;

constexpr auto kLocalhost = "127.0.0.1";

class LocalServer {
 public:
  LocalServer() {
    server_.Get("/json/stations/search", [](const httplib::Request&, httplib::Response& res) {
      res.set_content(load_fixture("stations.json"), "application/json");
    });
    server_.Get("/json/stats", [](const httplib::Request&, httplib::Response& res) {
      res.set_content(load_fixture("stats.json"), "application/json");
    });
    server_.Get("/echo-agent", [](const httplib::Request& req, httplib::Response& res) {
      res.set_content(req.get_header_value("User-Agent") + "|" + req.get_header_value("Accept"),
                      "text/plain");
    });
    server_.Get("/status/404", [](const httplib::Request&, httplib::Response& res) {
      res.status = 404;
      res.set_content("missing", "text/plain");
    });
    server_.Get("/redirect", [](const httplib::Request&, httplib::Response& res) {
      res.set_redirect("/json/stats");
    });
    server_.Get("/big", [](const httplib::Request&, httplib::Response& res) {
      res.set_content(std::string(512 * 1024, 'x'), "text/plain");
    });
    server_.Get("/slow", [this](const httplib::Request&, httplib::Response& res) {
      // Sleeps in small steps so that the server can shut down quickly.
      const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!stopping_ && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      res.set_content("late", "text/plain");
    });
    server_.Get("/client-port", [](const httplib::Request& req, httplib::Response& res) {
      res.set_content(std::to_string(req.remote_port), "text/plain");
    });
    server_.Post("/echo-body", [](const httplib::Request& req, httplib::Response& res) {
      res.set_content(req.body, "text/plain");
    });

    // Avoids 40 ms Nagle/delayed-ACK stalls on reused connections.
    server_.set_tcp_nodelay(true);
    port_ = server_.bind_to_any_port(kLocalhost);
    thread_ = std::thread([this] { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  LocalServer(const LocalServer&) = delete;
  LocalServer& operator=(const LocalServer&) = delete;
  LocalServer(LocalServer&&) = delete;
  LocalServer& operator=(LocalServer&&) = delete;

  ~LocalServer() {
    stopping_ = true;
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  [[nodiscard]] std::string base_url() const {
    return std::string("http://") + kLocalhost + ":" + std::to_string(port_);
  }

  [[nodiscard]] int port() const { return port_; }

 private:
  httplib::Server server_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  int port_ = 0;
};

// A port that refuses connections: run a server, then shut it down so that
// its listening socket is closed.
int unused_port() {
  httplib::Server server;
  const int port = server.bind_to_any_port(kLocalhost);
  std::thread thread([&server] { server.listen_after_bind(); });
  server.wait_until_ready();
  server.stop();
  thread.join();
  return port;
}

HttpRequest make_request(const std::string& url) {
  HttpRequest request;
  request.url = url;
  request.headers = {{"User-Agent", "curl-test/1.0"}, {"Accept", "application/json"}};
  request.timeout = std::chrono::milliseconds(5000);
  request.connect_timeout = std::chrono::milliseconds(2000);
  return request;
}

class CurlTransportTest : public ::testing::Test {
 protected:
  LocalServer server_;
  CurlTransport transport_;
};

TEST_F(CurlTransportTest, PerformsGetRequest) {
  const auto response = transport_.perform(make_request(server_.base_url() + "/json/stats"));
  ASSERT_TRUE(response) << to_string(response.error());
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(response->body, load_fixture("stats.json"));
}

TEST_F(CurlTransportTest, SendsHeaders) {
  const auto response = transport_.perform(make_request(server_.base_url() + "/echo-agent"));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->body, "curl-test/1.0|application/json");
}

TEST_F(CurlTransportTest, ReturnsErrorStatusAsResponse) {
  const auto response = transport_.perform(make_request(server_.base_url() + "/status/404"));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 404);
  EXPECT_EQ(response->body, "missing");
}

TEST_F(CurlTransportTest, FollowsRedirects) {
  const auto response = transport_.perform(make_request(server_.base_url() + "/redirect"));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(response->body, load_fixture("stats.json"));
}

TEST_F(CurlTransportTest, EnforcesResponseSizeLimit) {
  HttpRequest request = make_request(server_.base_url() + "/big");
  request.max_response_bytes = 1000;
  const auto response = transport_.perform(request);
  ASSERT_FALSE(response);
  EXPECT_EQ(response.error().code, ErrorCode::ResponseTooLarge);

  request.max_response_bytes = 1024 * 1024;
  const auto allowed = transport_.perform(request);
  ASSERT_TRUE(allowed);
  EXPECT_EQ(allowed->body.size(), 512U * 1024U);
}

TEST_F(CurlTransportTest, TimesOut) {
  HttpRequest request = make_request(server_.base_url() + "/slow");
  request.timeout = std::chrono::milliseconds(300);
  const auto started = std::chrono::steady_clock::now();
  const auto response = transport_.perform(request);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  ASSERT_FALSE(response);
  EXPECT_EQ(response.error().code, ErrorCode::Timeout);
  EXPECT_LT(elapsed, std::chrono::milliseconds(3000));
}

TEST_F(CurlTransportTest, CanBeCancelled) {
  CancellationSource source;
  HttpRequest request = make_request(server_.base_url() + "/slow");
  request.timeout = std::chrono::milliseconds(10000);
  request.cancellation = source.token();
  std::thread canceller([&source] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    source.cancel();
  });
  const auto started = std::chrono::steady_clock::now();
  const auto response = transport_.perform(request);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  canceller.join();
  ASSERT_FALSE(response);
  EXPECT_EQ(response.error().code, ErrorCode::Cancelled);
  // libcurl calls the progress callback at least once per second.
  EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

TEST_F(CurlTransportTest, ReportsConnectionFailures) {
  HttpRequest request =
      make_request(std::string("http://") + kLocalhost + ":" + std::to_string(unused_port()) + "/");
  request.timeout = std::chrono::milliseconds(3000);
  request.connect_timeout = std::chrono::milliseconds(3000);
  const auto response = transport_.perform(request);
  ASSERT_FALSE(response);
  // Windows retries refused local connections, which may end in a timeout.
  EXPECT_TRUE(response.error().code == ErrorCode::Network ||
              response.error().code == ErrorCode::Timeout)
      << to_string(response.error());
}

TEST_F(CurlTransportTest, RejectsHeaderInjection) {
  HttpRequest request = make_request(server_.base_url() + "/json/stats");
  request.headers.push_back({"X-Test", "a\r\nX-Injected: 1"});
  const auto response = transport_.perform(request);
  ASSERT_FALSE(response);
  EXPECT_EQ(response.error().code, ErrorCode::InvalidArgument);
}

TEST_F(CurlTransportTest, RejectsUnsupportedProtocols) {
  const auto response = transport_.perform(make_request("file:///etc/passwd"));
  ASSERT_FALSE(response);
  EXPECT_EQ(response.error().code, ErrorCode::Network);
}

TEST_F(CurlTransportTest, SendsPostBody) {
  HttpRequest request = make_request(server_.base_url() + "/echo-body");
  request.method = HttpMethod::Post;
  request.body = "name=radio&x=1";
  const auto response = transport_.perform(request);
  ASSERT_TRUE(response) << to_string(response.error());
  EXPECT_EQ(response->body, "name=radio&x=1");
}

TEST_F(CurlTransportTest, ReusesConnections) {
  const auto first = transport_.perform(make_request(server_.base_url() + "/client-port"));
  const auto second = transport_.perform(make_request(server_.base_url() + "/client-port"));
  ASSERT_TRUE(first) << to_string(first.error());
  ASSERT_TRUE(second) << to_string(second.error());
  // The same client port means the same TCP connection.
  EXPECT_EQ(first->body, second->body);
}

TEST_F(CurlTransportTest, ReuseCanBeDisabled) {
  CurlTransportOptions options;
  options.max_idle_handles = 0;
  CurlTransport transport(options);
  const auto first = transport.perform(make_request(server_.base_url() + "/client-port"));
  const auto second = transport.perform(make_request(server_.base_url() + "/client-port"));
  ASSERT_TRUE(first) << to_string(first.error());
  ASSERT_TRUE(second) << to_string(second.error());
  EXPECT_NE(first->body, second->body);
}

TEST_F(CurlTransportTest, PooledHandlesDoNotLeakSettings) {
  // A POST followed by a GET on the same pooled handle must be a plain GET.
  HttpRequest post = make_request(server_.base_url() + "/echo-body");
  post.method = HttpMethod::Post;
  post.body = "payload";
  ASSERT_TRUE(transport_.perform(post));

  HttpRequest limited = make_request(server_.base_url() + "/big");
  limited.max_response_bytes = 10;
  const auto too_large = transport_.perform(limited);
  ASSERT_FALSE(too_large);
  EXPECT_EQ(too_large.error().code, ErrorCode::ResponseTooLarge);

  const auto get = transport_.perform(make_request(server_.base_url() + "/json/stats"));
  ASSERT_TRUE(get) << to_string(get.error());
  EXPECT_EQ(get->status, 200);
  EXPECT_EQ(get->body, load_fixture("stats.json"));
}

TEST_F(CurlTransportTest, ConcurrentRequests) {
  // Threads plus pooled handles stay below the test server's eight workers,
  // which serve one keep-alive connection each.
  constexpr int kThreads = 4;
  constexpr int kRequestsPerThread = 20;
  std::atomic<int> successes{0};
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < kRequestsPerThread; ++i) {
        const auto response = transport_.perform(make_request(server_.base_url() + "/json/stats"));
        if (response && response->status == 200) {
          ++successes;
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(successes.load(), kThreads * kRequestsPerThread);
}

TEST_F(CurlTransportTest, ClientEndToEndWithFailover) {
  ClientOptions options;
  options.user_agent = "e2e-test/1.0";
  options.shuffle_servers = false;
  options.timeout = std::chrono::milliseconds(15000);
  options.attempt_timeout = std::chrono::milliseconds(5000);
  options.transport = std::make_shared<CurlTransport>();
  // The first server refuses connections; the client must fail over.
  options.discovery = std::make_shared<StaticServerDiscovery>(std::vector<std::string>{
      std::string("http://") + kLocalhost + ":" + std::to_string(unused_port()),
      server_.base_url()});
  auto client = Client::create(std::move(options));
  ASSERT_TRUE(client) << to_string(client.error());

  StationSearch search;
  search.name = "jazz";
  const auto stations = client->search_stations(search);
  ASSERT_TRUE(stations) << to_string(stations.error());
  ASSERT_EQ(stations->size(), 2U);
  EXPECT_EQ(stations->front().name, "Example Jazz Radio");

  const auto stats = client->stats();
  ASSERT_TRUE(stats) << to_string(stats.error());
  EXPECT_EQ(stats->status, "OK");
}

}  // namespace
}  // namespace radiobrowser
