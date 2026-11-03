// SPDX-License-Identifier: MIT

#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace radiobrowser {
namespace {

using test::CountingDiscovery;
using test::FakeTransport;
using test::header_value;
using test::load_fixture;
using test::MockTransport;
using test::ok_response;
using ::testing::Field;
using ::testing::Return;

const std::vector<std::string> kServers{"https://s1.test", "https://s2.test", "https://s3.test"};

ClientOptions base_options() {
  ClientOptions options;
  options.user_agent = "unit-test/1.0";
  options.shuffle_servers = false;
  options.timeout = std::chrono::milliseconds(5000);
  options.attempt_timeout = std::chrono::milliseconds(2000);
  options.connect_timeout = std::chrono::milliseconds(1000);
  return options;
}

Client make_client(std::shared_ptr<HttpTransport> transport,
                   std::shared_ptr<ServerDiscovery> discovery,
                   ClientOptions options = base_options()) {
  options.transport = std::move(transport);
  options.discovery = std::move(discovery);
  auto client = Client::create(std::move(options));
  if (!client) {
    throw std::runtime_error("Client::create failed: " + to_string(client.error()));
  }
  return std::move(client).value();
}

// Strips the scheme and host so that tests can compare paths.
std::string path_of(const std::string& url) {
  const auto scheme_end = url.find("://");
  const auto path_start = url.find('/', scheme_end + 3);
  return url.substr(path_start);
}

std::shared_ptr<FakeTransport> transport_returning(std::string body) {
  return std::make_shared<FakeTransport>(
      [body = std::move(body)](const HttpRequest&) -> Result<HttpResponse> {
        return ok_response(body);
      });
}

// --- Creation ------------------------------------------------------------------

TEST(ClientCreateTest, ValidatesOptions) {
  auto with_options = [](auto&& mutate) {
    ClientOptions options = base_options();
    options.transport = transport_returning("[]");
    options.discovery = std::make_shared<CountingDiscovery>(kServers);
    mutate(options);
    return Client::create(std::move(options));
  };

  const auto bad_agent = with_options([](ClientOptions& o) { o.user_agent = "a\r\nX-Evil: 1"; });
  ASSERT_FALSE(bad_agent);
  EXPECT_EQ(bad_agent.error().code, ErrorCode::InvalidArgument);

  const auto bad_timeout =
      with_options([](ClientOptions& o) { o.timeout = std::chrono::milliseconds(0); });
  ASSERT_FALSE(bad_timeout);
  EXPECT_EQ(bad_timeout.error().code, ErrorCode::InvalidArgument);

  const auto bad_limit = with_options([](ClientOptions& o) { o.default_limit = 0; });
  ASSERT_FALSE(bad_limit);
  EXPECT_EQ(bad_limit.error().code, ErrorCode::InvalidArgument);

  const auto good = with_options([](ClientOptions&) {});
  EXPECT_TRUE(good);
}

TEST(ClientCreateTest, DefaultTransportDependsOnBuild) {
  ClientOptions options = base_options();
  options.discovery = std::make_shared<CountingDiscovery>(kServers);
  const auto client = Client::create(std::move(options));
#if RADIOBROWSER_HAS_CURL
  EXPECT_TRUE(client);
#else
  ASSERT_FALSE(client);
  EXPECT_EQ(client.error().code, ErrorCode::InvalidArgument);
#endif
}

TEST(ClientCreateTest, DefaultUserAgentContainsVersion) {
  ClientOptions options = base_options();
  options.user_agent.clear();
  const Client client = make_client(transport_returning("[]"),
                                    std::make_shared<CountingDiscovery>(kServers), options);
  EXPECT_EQ(client.user_agent(), std::string("radiobrowser-cpp/") + kVersion);
}

// --- Request building ----------------------------------------------------------

TEST(ClientRequestTest, SearchBuildsQueryAndHeaders) {
  auto transport = transport_returning(load_fixture("stations.json"));
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));

  StationSearch search;
  search.name = "Jazz FM";
  search.country_code = "DE";
  search.tag_list = {"jazz", "news"};
  search.bitrate_min = 64;
  search.is_https = true;
  search.order = StationOrder::Votes;
  search.reverse = true;
  search.limit = 10;

  const auto stations = client.search_stations(search);
  ASSERT_TRUE(stations) << to_string(stations.error());
  EXPECT_EQ(stations->size(), 2U);

  const auto requests = transport->requests();
  ASSERT_EQ(requests.size(), 1U);
  const HttpRequest& request = requests.front();
  EXPECT_EQ(request.url,
            "https://s1.test/json/stations/search?name=Jazz%20FM&countrycode=DE"
            "&tagList=jazz%2Cnews&bitrateMin=64&is_https=true&order=votes&reverse=true"
            "&hidebroken=true&limit=10");
  EXPECT_EQ(request.method, HttpMethod::Get);
  EXPECT_EQ(header_value(request, "User-Agent"), "unit-test/1.0");
  EXPECT_EQ(header_value(request, "Accept"), "application/json");
  EXPECT_LE(request.timeout, std::chrono::milliseconds(2000));
  EXPECT_LE(request.connect_timeout, std::chrono::milliseconds(1000));
  EXPECT_EQ(request.max_response_bytes, base_options().max_response_bytes);
}

TEST(ClientRequestTest, AppliesDefaults) {
  auto transport = transport_returning("[]");
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
  ASSERT_TRUE(client.stations());
  ASSERT_TRUE(client.search_stations({}));
  const auto urls = transport->urls();
  ASSERT_EQ(urls.size(), 2U);
  EXPECT_EQ(path_of(urls[0]), "/json/stations?order=name&reverse=false&hidebroken=true&limit=100");
  EXPECT_EQ(path_of(urls[1]),
            "/json/stations/search?order=name&reverse=false&hidebroken=true&limit=100");
}

TEST(ClientRequestTest, ListEndpoints) {
  auto transport = transport_returning("[]");
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));

  ListQuery countries;
  countries.filter = "\xC3\x96sterreich";
  ASSERT_TRUE(client.countries(countries));

  ListQuery tags;
  tags.filter = "jazz";
  tags.order = ListOrder::StationCount;
  tags.reverse = true;
  tags.limit = 5;
  ASSERT_TRUE(client.tags(tags));

  StateQuery states;
  states.country = "Germany";
  states.filter = "Hes";
  ASSERT_TRUE(client.states(states));

  ListQuery all;
  all.hide_broken = false;
  all.offset = 10;
  ASSERT_TRUE(client.country_codes(all));
  ASSERT_TRUE(client.codecs(all));
  ASSERT_TRUE(client.languages(all));
  ASSERT_TRUE(client.states());

  const auto urls = transport->urls();
  ASSERT_EQ(urls.size(), 7U);
  EXPECT_EQ(path_of(urls[0]),
            "/json/countries/%C3%96sterreich?order=name&reverse=false&hidebroken=true");
  EXPECT_EQ(path_of(urls[1]),
            "/json/tags/jazz?order=stationcount&reverse=true&hidebroken=true&limit=5");
  EXPECT_EQ(path_of(urls[2]), "/json/states/Germany/Hes?order=name&reverse=false&hidebroken=true");
  EXPECT_EQ(path_of(urls[3]),
            "/json/countrycodes?order=name&reverse=false&hidebroken=false&offset=10");
  EXPECT_EQ(path_of(urls[4]), "/json/codecs?order=name&reverse=false&hidebroken=false&offset=10");
  EXPECT_EQ(path_of(urls[5]),
            "/json/languages?order=name&reverse=false&hidebroken=false&offset=10");
  EXPECT_EQ(path_of(urls[6]), "/json/states?order=name&reverse=false&hidebroken=true");
}

TEST(ClientRequestTest, StationsByField) {
  auto transport = transport_returning("[]");
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));

  ASSERT_TRUE(client.stations_by(StationField::Name, "BBC Radio", true));
  ASSERT_TRUE(client.stations_by(StationField::CountryCode, "de"));
  StationListQuery query;
  query.order = StationOrder::ClickCount;
  query.offset = 20;
  query.limit = 10;
  ASSERT_TRUE(client.stations_by(StationField::Tag, "rock", false, query));

  const auto urls = transport->urls();
  ASSERT_EQ(urls.size(), 3U);
  EXPECT_EQ(path_of(urls[0]),
            "/json/stations/bynameexact/BBC%20Radio?order=name&reverse=false&hidebroken=true"
            "&limit=100");
  EXPECT_EQ(path_of(urls[1]),
            "/json/stations/bycountrycodeexact/de?order=name&reverse=false&hidebroken=true"
            "&limit=100");
  EXPECT_EQ(path_of(urls[2]),
            "/json/stations/bytag/rock?order=clickcount&reverse=false&hidebroken=true&offset=20"
            "&limit=10");
}

TEST(ClientRequestTest, OtherEndpoints) {
  auto transport = transport_returning("[]");
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));

  ASSERT_TRUE(client.top_clicked());
  ASSERT_TRUE(client.top_voted());
  ASSERT_TRUE(client.recently_clicked());
  ASSERT_TRUE(client.recently_changed());
  PageQuery page;
  page.offset = 5;
  page.limit = 3;
  ASSERT_TRUE(client.broken_stations(page));
  ChangesQuery changes;
  changes.station_uuid = "abc";
  changes.last_change_uuid = "x";
  ASSERT_TRUE(client.station_changes(changes));
  ChecksQuery checks;
  checks.seconds = std::chrono::seconds(3600);
  checks.limit = 2;
  ASSERT_TRUE(client.checks(checks));
  ClicksQuery clicks;
  clicks.station_uuid = "abc";
  ASSERT_TRUE(client.clicks(clicks));
  ASSERT_TRUE(client.check_steps({"a", "b"}));
  ASSERT_TRUE(client.streaming_servers());
  ASSERT_TRUE(client.streaming_servers_by_uuid({"u1"}));
  ASSERT_TRUE(client.streaming_servers_by_station({"s1"}));
  ASSERT_TRUE(client.stations_by_server_uuid({"u1"}));
  ASSERT_TRUE(client.stations_by_url("http://stream.example.com/a b"));
  ASSERT_TRUE(client.stations_by_uuid({"u1", "u2"}));

  const std::vector<std::string> expected{
      "/json/stations/topclick?hidebroken=true&limit=100",
      "/json/stations/topvote?hidebroken=true&limit=100",
      "/json/stations/lastclick?hidebroken=true&limit=100",
      "/json/stations/lastchange?hidebroken=true&limit=100",
      "/json/stations/broken?offset=5&limit=3",
      "/json/stations/changed/abc?lastchangeuuid=x&limit=100",
      "/json/checks?seconds=3600&limit=2",
      "/json/clicks/abc",
      "/json/checksteps?uuids=a%2Cb",
      "/json/streamingservers?limit=100",
      "/json/streamingservers/byserveruuid?uuids=u1",
      "/json/streamingservers/bystationuuid?uuids=s1",
      "/json/stations/byserveruuid?order=name&reverse=false&hidebroken=true&limit=100&uuids=u1",
      "/json/stations/byurl?url=http%3A%2F%2Fstream.example.com%2Fa%20b",
      "/json/stations/byuuid?uuids=u1%2Cu2"};
  const auto urls = transport->urls();
  ASSERT_EQ(urls.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(path_of(urls[i]), expected[i]) << "request " << i;
  }
}

TEST(ClientRequestTest, InvalidArgumentsDoNotTouchTheNetwork) {
  auto transport = transport_returning("[]");
  auto discovery = std::make_shared<CountingDiscovery>(kServers);
  const Client client = make_client(transport, discovery);

  EXPECT_EQ(client.stations_by(StationField::Name, "").error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(client.count_click("").error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(client.vote("").error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(client.station("").error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(client.stations_by_url("").error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(client.get_raw("json/stats").error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(client.get_raw("/json/stats now").error().code, ErrorCode::InvalidArgument);

  const auto none = client.stations_by_uuid({});
  ASSERT_TRUE(none);
  EXPECT_TRUE(none->empty());
  EXPECT_TRUE(client.check_steps({""}).value().empty());

  EXPECT_EQ(transport->request_count(), 0U);
  EXPECT_EQ(discovery->calls(), 0);
}

// --- Responses -----------------------------------------------------------------

TEST(ClientResponseTest, StationByUuid) {
  const std::string uuid = "960e57c5-0601-11e8-ae97-52543be04c81";
  auto transport = std::make_shared<FakeTransport>([&](const HttpRequest& request) {
    if (request.url.find("unknown") != std::string::npos) {
      return Result<HttpResponse>(ok_response("[]"));
    }
    return Result<HttpResponse>(ok_response(load_fixture("stations.json")));
  });
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));

  const auto station = client.station(uuid);
  ASSERT_TRUE(station) << to_string(station.error());
  EXPECT_EQ(station->station_uuid, uuid);
  EXPECT_EQ(path_of(transport->urls().front()), "/json/stations/byuuid?uuids=" + uuid);

  const auto missing = client.station("unknown");
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST(ClientResponseTest, CountClickAndVote) {
  auto transport = std::make_shared<FakeTransport>([](const HttpRequest& request) {
    if (request.url.find("/json/url/") != std::string::npos) {
      return Result<HttpResponse>(ok_response(load_fixture("click.json")));
    }
    return Result<HttpResponse>(ok_response(load_fixture("vote.json")));
  });
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));

  const auto click = client.count_click("960e57c5-0601-11e8-ae97-52543be04c81");
  ASSERT_TRUE(click);
  EXPECT_EQ(click->url, "https://stream.example.com/jazz_128.mp3");

  const auto vote = client.vote("960e57c5-0601-11e8-ae97-52543be04c81");
  ASSERT_TRUE(vote);
  EXPECT_FALSE(vote->ok);

  const auto urls = transport->urls();
  ASSERT_EQ(urls.size(), 2U);
  EXPECT_EQ(path_of(urls[0]), "/json/url/960e57c5-0601-11e8-ae97-52543be04c81");
  EXPECT_EQ(path_of(urls[1]), "/json/vote/960e57c5-0601-11e8-ae97-52543be04c81");
}

TEST(ClientResponseTest, ServerInformation) {
  auto transport = std::make_shared<FakeTransport>([](const HttpRequest& request) {
    if (request.url.ends_with("/json/stats")) {
      return Result<HttpResponse>(ok_response(load_fixture("stats.json")));
    }
    if (request.url.ends_with("/json/config")) {
      return Result<HttpResponse>(ok_response(load_fixture("config.json")));
    }
    return Result<HttpResponse>(ok_response(load_fixture("servers.json")));
  });
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));

  const auto stats = client.stats();
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->status, "OK");
  const auto config = client.config();
  ASSERT_TRUE(config);
  EXPECT_EQ(config->server_country_code, "DE");
  const auto mirrors = client.server_mirrors();
  ASSERT_TRUE(mirrors);
  EXPECT_EQ(mirrors->size(), 5U);
  const auto raw = client.get_raw("/json/stats");
  ASSERT_TRUE(raw);
  EXPECT_NE(raw->find("\"status\": \"OK\""), std::string::npos);
}

TEST(ClientResponseTest, InvalidJsonIsReported) {
  auto transport = transport_returning("<html>oops</html>");
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
  const auto stations = client.stations();
  ASSERT_FALSE(stations);
  EXPECT_EQ(stations.error().code, ErrorCode::InvalidResponse);
  EXPECT_EQ(transport->request_count(), 1U);
}

TEST(ClientResponseTest, WorksWithGMock) {
  auto transport = std::make_shared<MockTransport>();
  EXPECT_CALL(*transport, perform(Field(&HttpRequest::url, "https://s1.test/json/stats")))
      .WillOnce(Return(Result<HttpResponse>(ok_response(load_fixture("stats.json")))));
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
  const auto stats = client.stats();
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->stations, 54783);
}

// --- Failover ------------------------------------------------------------------

TEST(ClientFailoverTest, FailsOverOnNetworkErrorAndRemembersServer) {
  auto transport = std::make_shared<FakeTransport>([](const HttpRequest& request) {
    if (request.url.starts_with("https://s1.test")) {
      return Result<HttpResponse>(Error{ErrorCode::Network, "connection refused", 0});
    }
    return Result<HttpResponse>(ok_response("[]"));
  });
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));

  ASSERT_TRUE(client.stations());
  ASSERT_TRUE(client.stations());
  const auto urls = transport->urls();
  ASSERT_EQ(urls.size(), 3U);
  EXPECT_TRUE(urls[0].starts_with("https://s1.test"));
  EXPECT_TRUE(urls[1].starts_with("https://s2.test"));
  EXPECT_TRUE(urls[2].starts_with("https://s2.test"));  // remembered
}

TEST(ClientFailoverTest, FailsOverOnRetryableStatus) {
  auto transport = std::make_shared<FakeTransport>([](const HttpRequest& request) {
    if (request.url.starts_with("https://s1.test")) {
      return Result<HttpResponse>(HttpResponse{503, "busy"});
    }
    return Result<HttpResponse>(ok_response("[]"));
  });
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
  ASSERT_TRUE(client.tags());
  EXPECT_EQ(transport->request_count(), 2U);
}

TEST(ClientFailoverTest, DoesNotFailOverOnClientError) {
  auto transport = std::make_shared<FakeTransport>(
      [](const HttpRequest&) { return Result<HttpResponse>(HttpResponse{404, "not found"}); });
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
  const auto result = client.tags();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::HttpStatus);
  EXPECT_EQ(result.error().http_status, 404);
  EXPECT_EQ(transport->request_count(), 1U);
}

TEST(ClientFailoverTest, AllServersFailedTriggersRediscovery) {
  auto transport = std::make_shared<FakeTransport>([](const HttpRequest&) {
    return Result<HttpResponse>(Error{ErrorCode::Network, "unreachable", 0});
  });
  auto discovery = std::make_shared<CountingDiscovery>(kServers);
  const Client client = make_client(transport, discovery);

  const auto first = client.stats();
  ASSERT_FALSE(first);
  EXPECT_EQ(first.error().code, ErrorCode::AllServersFailed);
  EXPECT_NE(first.error().message.find("s3.test"), std::string::npos);
  EXPECT_EQ(transport->request_count(), 3U);

  static_cast<void>(client.stats());
  EXPECT_EQ(discovery->calls(), 2);
}

TEST(ClientFailoverTest, NoFailoverForCancellationOrOversizedResponses) {
  for (const ErrorCode code : {ErrorCode::Cancelled, ErrorCode::ResponseTooLarge}) {
    auto transport = std::make_shared<FakeTransport>(
        [code](const HttpRequest&) { return Result<HttpResponse>(Error{code, "stop", 0}); });
    const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
    const auto result = client.stations();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, code);
    EXPECT_EQ(transport->request_count(), 1U);
  }
}

TEST(ClientFailoverTest, TransportExceptionsBecomeErrors) {
  auto transport = std::make_shared<FakeTransport>(
      [](const HttpRequest&) -> Result<HttpResponse> { throw std::runtime_error("boom"); });
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
  const auto result = client.stats();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::Internal);
  EXPECT_NE(result.error().message.find("boom"), std::string::npos);
}

// --- Server list -----------------------------------------------------------------

TEST(ClientServersTest, CachesServerListWithinTtl) {
  auto discovery = std::make_shared<CountingDiscovery>(kServers);
  const Client client = make_client(transport_returning("[]"), discovery);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(client.tags());
  }
  EXPECT_EQ(discovery->calls(), 1);

  ASSERT_TRUE(client.refresh_servers());
  EXPECT_EQ(discovery->calls(), 2);
}

TEST(ClientServersTest, ZeroTtlRediscoversEveryTime) {
  ClientOptions options = base_options();
  options.server_list_ttl = std::chrono::seconds(0);
  auto discovery = std::make_shared<CountingDiscovery>(kServers);
  const Client client = make_client(transport_returning("[]"), discovery, options);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(client.tags());
  }
  EXPECT_EQ(discovery->calls(), 3);
}

TEST(ClientServersTest, ReturnsDiscoveredServers) {
  const Client ordered =
      make_client(transport_returning("[]"), std::make_shared<CountingDiscovery>(kServers));
  const auto servers = ordered.servers();
  ASSERT_TRUE(servers);
  EXPECT_EQ(*servers, kServers);

  ClientOptions options = base_options();
  options.shuffle_servers = true;
  const Client shuffled = make_client(transport_returning("[]"),
                                      std::make_shared<CountingDiscovery>(kServers), options);
  auto shuffled_servers = shuffled.servers().value();
  std::sort(shuffled_servers.begin(), shuffled_servers.end());
  EXPECT_EQ(shuffled_servers, kServers);
}

TEST(ClientServersTest, DiscoveryFailuresPropagate) {
  auto transport = transport_returning("[]");
  const Client none =
      make_client(transport, std::make_shared<CountingDiscovery>(std::vector<std::string>{}));
  const auto result = none.stats();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::NoServers);

  const Client invalid = make_client(
      transport, std::make_shared<CountingDiscovery>(std::vector<std::string>{"not a url"}));
  const auto invalid_result = invalid.stats();
  ASSERT_FALSE(invalid_result);
  EXPECT_EQ(invalid_result.error().code, ErrorCode::NoServers);
  EXPECT_EQ(transport->request_count(), 0U);
}

// --- Deadlines and cancellation ------------------------------------------------

TEST(ClientDeadlineTest, DeadlineCoversAllFailoverAttempts) {
  // Every server hangs until the attempt timeout expires.
  auto transport = std::make_shared<FakeTransport>([](const HttpRequest& request) {
    std::this_thread::sleep_for(request.timeout);
    return Result<HttpResponse>(Error{ErrorCode::Timeout, "attempt timed out", 0});
  });
  ClientOptions options = base_options();
  options.timeout = std::chrono::milliseconds(300);
  options.attempt_timeout = std::chrono::milliseconds(200);
  const Client client =
      make_client(transport, std::make_shared<CountingDiscovery>(kServers), options);

  const auto started = std::chrono::steady_clock::now();
  const auto result = client.stations();
  const auto elapsed = std::chrono::steady_clock::now() - started;

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::Timeout);
  EXPECT_LT(elapsed, std::chrono::milliseconds(3000));
  const auto requests = transport->requests();
  ASSERT_GE(requests.size(), 2U);
  EXPECT_LE(requests[0].timeout, std::chrono::milliseconds(200));
  EXPECT_LE(requests[1].timeout, std::chrono::milliseconds(100));
}

TEST(ClientDeadlineTest, PerCallTimeoutOverridesDefault) {
  auto transport = transport_returning("[]");
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
  RequestOptions request;
  request.timeout = std::chrono::milliseconds(50);
  ASSERT_TRUE(client.tags({}, request));
  EXPECT_LE(transport->requests().front().timeout, std::chrono::milliseconds(50));
}

TEST(ClientDeadlineTest, CancelledBeforeTheCall) {
  auto transport = transport_returning("[]");
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
  CancellationSource source;
  source.cancel();
  RequestOptions request;
  request.cancellation = source.token();
  const auto result = client.stations({}, request);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
  EXPECT_EQ(transport->request_count(), 0U);
}

TEST(ClientDeadlineTest, CancellationReachesTheTransport) {
  CancellationSource source;
  auto transport = std::make_shared<FakeTransport>([&source](const HttpRequest& request) {
    EXPECT_TRUE(request.cancellation.can_be_cancelled());
    source.cancel();
    EXPECT_TRUE(request.cancellation.is_cancelled());
    return Result<HttpResponse>(Error{ErrorCode::Cancelled, "cancelled", 0});
  });
  const Client client = make_client(transport, std::make_shared<CountingDiscovery>(kServers));
  RequestOptions request;
  request.cancellation = source.token();
  const auto result = client.stations({}, request);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
  EXPECT_EQ(transport->request_count(), 1U);
}

// --- Logging -------------------------------------------------------------------

TEST(ClientLoggingTest, ReportsFailover) {
  auto transport = std::make_shared<FakeTransport>([](const HttpRequest& request) {
    if (request.url.starts_with("https://s1.test")) {
      return Result<HttpResponse>(Error{ErrorCode::Network, "refused", 0});
    }
    return Result<HttpResponse>(ok_response("[]"));
  });
  std::vector<std::pair<LogLevel, std::string>> messages;
  std::mutex mutex;
  ClientOptions options = base_options();
  options.logger = [&](LogLevel level, std::string_view message) {
    const std::lock_guard<std::mutex> lock(mutex);
    messages.emplace_back(level, std::string(message));
  };
  const Client client =
      make_client(transport, std::make_shared<CountingDiscovery>(kServers), options);
  ASSERT_TRUE(client.tags());

  const bool warned = std::any_of(messages.begin(), messages.end(), [](const auto& entry) {
    return entry.first == LogLevel::Warning && entry.second.find("s1.test") != std::string::npos;
  });
  EXPECT_TRUE(warned);
}

TEST(ClientLoggingTest, ThrowingLoggerIsIgnored) {
  ClientOptions options = base_options();
  options.logger = [](LogLevel, std::string_view) { throw std::runtime_error("logger failed"); };
  const Client client = make_client(transport_returning("[]"),
                                    std::make_shared<CountingDiscovery>(kServers), options);
  EXPECT_TRUE(client.tags());
}

// --- Threading and lifetime ----------------------------------------------------

TEST(ClientThreadingTest, ConcurrentCallsShareOneDiscovery) {
  auto transport = transport_returning(load_fixture("stats.json"));
  auto discovery = std::make_shared<CountingDiscovery>(kServers);
  const Client client = make_client(transport, discovery);

  constexpr int kThreads = 8;
  constexpr int kCallsPerThread = 25;
  std::atomic<int> successes{0};
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < kCallsPerThread; ++i) {
        if (client.stats()) {
          ++successes;
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(successes.load(), kThreads * kCallsPerThread);
  EXPECT_EQ(discovery->calls(), 1);
  EXPECT_EQ(transport->request_count(), static_cast<std::size_t>(kThreads * kCallsPerThread));
}

TEST(ClientLifetimeTest, MovedFromClientReportsError) {
  Client original =
      make_client(transport_returning("[]"), std::make_shared<CountingDiscovery>(kServers));
  const Client moved = std::move(original);
  EXPECT_TRUE(moved.tags());
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved): tested on purpose
  const auto result = original.tags();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::Internal);
  EXPECT_TRUE(original.user_agent().empty());  // NOLINT(bugprone-use-after-move)
}

}  // namespace
}  // namespace radiobrowser
