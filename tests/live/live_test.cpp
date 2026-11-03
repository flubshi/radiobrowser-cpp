// SPDX-License-Identifier: MIT
// Tests against the real radio-browser API. They need internet access and are
// therefore built separately (RADIOBROWSER_BUILD_LIVE_TESTS) and labelled
// "live". They never count clicks or votes, so they do not distort the
// statistics of the public service.

#include <radiobrowser/radiobrowser.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace radiobrowser {
namespace {

std::string live_user_agent() {
  return std::string("radiobrowser-cpp-live-tests/") + kVersion;
}

ClientOptions live_options() {
  ClientOptions options;
  options.user_agent = live_user_agent();
  options.timeout = std::chrono::seconds(45);
  options.attempt_timeout = std::chrono::seconds(15);
  return options;
}

DiscoveryRequest live_discovery_request(HttpTransport* transport) {
  DiscoveryRequest request;
  request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  request.transport = transport;
  request.user_agent = live_user_agent();
  request.max_response_bytes = 4 * 1024 * 1024;
  return request;
}

class LiveTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    auto created = Client::create(live_options());
    ASSERT_TRUE(created) << to_string(created.error());
    client_ = std::make_unique<Client>(std::move(created).value());
  }

  static void TearDownTestSuite() { client_.reset(); }

  static const Client& client() { return *client_; }

 private:
  static std::unique_ptr<Client> client_;
};

std::unique_ptr<Client> LiveTest::client_;

TEST(LiveDiscoveryTest, DnsDiscoveryFindsServers) {
  DnsDiscoveryOptions options;
  options.timeout = std::chrono::seconds(20);
  DnsServerDiscovery discovery(options);
  const auto servers = discovery.discover(live_discovery_request(nullptr));
  ASSERT_TRUE(servers) << to_string(servers.error());
  EXPECT_FALSE(servers->empty());
  for (const auto& server : *servers) {
    EXPECT_EQ(server.rfind("https://", 0), 0U) << server;
  }
}

TEST(LiveDiscoveryTest, HttpDiscoveryFindsServers) {
  CurlTransport transport;
  HttpServerDiscovery discovery;
  const auto servers = discovery.discover(live_discovery_request(&transport));
  ASSERT_TRUE(servers) << to_string(servers.error());
  EXPECT_FALSE(servers->empty());
}

TEST_F(LiveTest, ServersAreAvailable) {
  const auto servers = client().servers();
  ASSERT_TRUE(servers) << to_string(servers.error());
  EXPECT_FALSE(servers->empty());
}

TEST_F(LiveTest, Stats) {
  const auto stats = client().stats();
  ASSERT_TRUE(stats) << to_string(stats.error());
  EXPECT_EQ(stats->status, "OK");
  EXPECT_GT(stats->stations, 1000);
}

TEST_F(LiveTest, SearchAndLookupByUuid) {
  StationSearch search;
  search.tag = "jazz";
  search.order = StationOrder::ClickCount;
  search.reverse = true;
  search.limit = 5;
  const auto stations = client().search_stations(search);
  ASSERT_TRUE(stations) << to_string(stations.error());
  ASSERT_FALSE(stations->empty());
  EXPECT_LE(stations->size(), 5U);
  for (const auto& station : *stations) {
    EXPECT_FALSE(station.station_uuid.empty());
    EXPECT_FALSE(station.stream_url().empty());
  }

  const auto station = client().station(stations->front().station_uuid);
  ASSERT_TRUE(station) << to_string(station.error());
  EXPECT_EQ(station->station_uuid, stations->front().station_uuid);
}

TEST_F(LiveTest, CountriesContainGermany) {
  const auto countries = client().countries();
  ASSERT_TRUE(countries) << to_string(countries.error());
  const bool has_germany =
      std::any_of(countries->begin(), countries->end(),
                  [](const Country& country) { return country.iso_3166_1 == "DE"; });
  EXPECT_TRUE(has_germany);
}

TEST_F(LiveTest, FilteredTags) {
  ListQuery query;
  query.filter = "jazz";
  query.order = ListOrder::StationCount;
  query.reverse = true;
  query.limit = 10;
  const auto tags = client().tags(query);
  ASSERT_TRUE(tags) << to_string(tags.error());
  ASSERT_FALSE(tags->empty());
  EXPECT_LE(tags->size(), 10U);
}

TEST_F(LiveTest, TopVoted) {
  PageQuery query;
  query.limit = 3;
  const auto stations = client().top_voted(query);
  ASSERT_TRUE(stations) << to_string(stations.error());
  EXPECT_EQ(stations->size(), 3U);
}

TEST_F(LiveTest, StationsByCountryCode) {
  StationListQuery query;
  query.limit = 5;
  const auto stations = client().stations_by(StationField::CountryCode, "DE", true, query);
  ASSERT_TRUE(stations) << to_string(stations.error());
  ASSERT_FALSE(stations->empty());
  for (const auto& station : *stations) {
    EXPECT_EQ(station.country_code, "DE");
  }
}

}  // namespace
}  // namespace radiobrowser
