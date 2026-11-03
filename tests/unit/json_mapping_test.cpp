// SPDX-License-Identifier: MIT

#include "detail/json_mapping.hpp"
#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace radiobrowser::detail {
namespace {

using test::load_fixture;

std::int64_t epoch(const std::optional<Timestamp>& timestamp) {
  return timestamp ? timestamp->time_since_epoch().count() : -1;
}

TEST(JsonMappingTest, MapsCompleteStation) {
  const auto stations = parse_stations(load_fixture("stations.json"));
  ASSERT_TRUE(stations) << to_string(stations.error());
  ASSERT_EQ(stations->size(), 2U);

  const Station& station = stations->at(0);
  EXPECT_EQ(station.station_uuid, "960e57c5-0601-11e8-ae97-52543be04c81");
  EXPECT_EQ(station.change_uuid, "610cafba-71d8-40fc-bf68-1456ec973b9d");
  EXPECT_EQ(station.server_uuid, "b3fb3d0e-6a8b-4c9e-9c4f-0c1e2a3b4c5d");
  EXPECT_EQ(station.name, "Example Jazz Radio");
  EXPECT_EQ(station.url, "http://stream.example.com/jazz.pls");
  EXPECT_EQ(station.url_resolved, "https://stream.example.com/jazz_128.mp3");
  EXPECT_EQ(station.stream_url(), "https://stream.example.com/jazz_128.mp3");
  EXPECT_EQ(station.homepage, "https://jazz.example.com/");
  EXPECT_EQ(station.favicon, "https://jazz.example.com/favicon.png");
  EXPECT_EQ(station.tags, (std::vector<std::string>{"jazz", "smooth jazz", "lounge"}));
  EXPECT_EQ(station.country, "Germany");
  EXPECT_EQ(station.country_code, "DE");
  EXPECT_EQ(station.iso_3166_2, "DE-HE");
  EXPECT_EQ(station.state, "Hessen");
  EXPECT_EQ(station.languages, (std::vector<std::string>{"german", "english"}));
  EXPECT_EQ(station.language_codes, (std::vector<std::string>{"de", "en"}));
  EXPECT_EQ(station.votes, 1234);
  EXPECT_EQ(station.codec, "MP3");
  EXPECT_EQ(station.bitrate, 128);
  EXPECT_FALSE(station.hls);
  EXPECT_TRUE(station.last_check_ok);
  EXPECT_EQ(epoch(station.last_change_time), 1740823872);
  EXPECT_EQ(epoch(station.last_check_time), 1793384195);
  EXPECT_EQ(epoch(station.last_check_ok_time), 1793384195);
  EXPECT_TRUE(station.last_local_check_time.has_value());
  EXPECT_TRUE(station.click_timestamp.has_value());
  EXPECT_EQ(station.click_count, 512);
  EXPECT_EQ(station.click_trend, -3);
  EXPECT_FALSE(station.ssl_error);
  ASSERT_TRUE(station.geo_lat);
  EXPECT_DOUBLE_EQ(*station.geo_lat, 50.1109);
  ASSERT_TRUE(station.geo_long);
  EXPECT_DOUBLE_EQ(*station.geo_long, 8.6821);
  EXPECT_FALSE(station.has_extended_info);
}

TEST(JsonMappingTest, MapsLegacyStationWithNullsAndEmptyStrings) {
  const auto stations = parse_stations(load_fixture("stations.json"));
  ASSERT_TRUE(stations);
  const Station& station = stations->at(1);
  EXPECT_EQ(station.name, "Legacy Station");  // trimmed
  EXPECT_TRUE(station.server_uuid.empty());
  EXPECT_TRUE(station.url_resolved.empty());
  EXPECT_EQ(station.stream_url(), "http://legacy.example.org:8000/live");
  EXPECT_TRUE(station.tags.empty());
  EXPECT_TRUE(station.languages.empty());
  EXPECT_TRUE(station.language_codes.empty());
  EXPECT_TRUE(station.iso_3166_2.empty());
  EXPECT_EQ(station.codec, "AAC+");
  EXPECT_TRUE(station.hls);
  EXPECT_FALSE(station.last_check_ok);
  // Only the legacy field is present.
  EXPECT_EQ(epoch(station.last_change_time), 1576175822);
  EXPECT_FALSE(station.last_check_ok_time.has_value());
  EXPECT_FALSE(station.click_timestamp.has_value());
  EXPECT_TRUE(station.ssl_error);
  EXPECT_FALSE(station.geo_lat.has_value());
  EXPECT_FALSE(station.geo_long.has_value());
  EXPECT_TRUE(station.has_extended_info);
}

TEST(JsonMappingTest, ToleratesUnexpectedTypes) {
  const auto stations = parse_stations(load_fixture("stations_tolerant.json"));
  ASSERT_TRUE(stations) << to_string(stations.error());
  // Non-object array elements are skipped.
  ASSERT_EQ(stations->size(), 2U);

  const Station& strings = stations->at(0);
  EXPECT_EQ(strings.tags, (std::vector<std::string>{"rock", "pop"}));
  EXPECT_EQ(strings.languages, (std::vector<std::string>{"english"}));
  EXPECT_EQ(strings.votes, 17);
  EXPECT_EQ(strings.bitrate, 192);
  EXPECT_TRUE(strings.hls);
  EXPECT_TRUE(strings.last_check_ok);
  EXPECT_EQ(strings.click_count, 9);
  EXPECT_EQ(strings.click_trend, -2);
  EXPECT_FALSE(strings.ssl_error);
  ASSERT_TRUE(strings.geo_lat);
  EXPECT_DOUBLE_EQ(*strings.geo_lat, 48.137);
  ASSERT_TRUE(strings.geo_long);
  EXPECT_DOUBLE_EQ(*strings.geo_long, 11.575);
  EXPECT_FALSE(strings.has_extended_info);
  // The broken ISO field falls back to the legacy field.
  EXPECT_EQ(epoch(strings.last_change_time), 1709251199);

  const Station& garbage = stations->at(1);
  EXPECT_EQ(garbage.station_uuid, "aaaaaaaa-0000-0000-0000-000000000002");
  EXPECT_EQ(garbage.votes, 0);
  EXPECT_EQ(garbage.bitrate, 1);
  EXPECT_FALSE(garbage.hls);
  EXPECT_FALSE(garbage.geo_lat.has_value());
  EXPECT_TRUE(garbage.name.empty());
}

TEST(JsonMappingTest, RejectsInvalidDocuments) {
  const auto invalid = parse_stations("[{\"name\": ");
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().code, ErrorCode::InvalidResponse);

  const auto object = parse_stations("{\"name\": \"x\"}");
  ASSERT_FALSE(object);
  EXPECT_EQ(object.error().code, ErrorCode::InvalidResponse);

  const auto html = parse_server_stats("<html>502 Bad Gateway</html>");
  ASSERT_FALSE(html);
  EXPECT_EQ(html.error().code, ErrorCode::InvalidResponse);

  const auto array = parse_vote_result("[]");
  ASSERT_FALSE(array);
  EXPECT_EQ(array.error().code, ErrorCode::InvalidResponse);

  const auto empty = parse_stations("");
  ASSERT_FALSE(empty);
  EXPECT_EQ(empty.error().code, ErrorCode::InvalidResponse);
}

TEST(JsonMappingTest, SurvivesDeeplyNestedInput) {
  constexpr std::size_t kDepth = 100000;
  std::string body = "[{\"name\":";
  body.append(kDepth, '[');
  body.append(kDepth, ']');
  body += ",\"stationuuid\":\"x\"}]";
  const auto stations = parse_stations(body);
  ASSERT_TRUE(stations);
  ASSERT_EQ(stations->size(), 1U);
  EXPECT_TRUE(stations->front().name.empty());
  EXPECT_EQ(stations->front().station_uuid, "x");

  std::string unterminated(kDepth, '[');
  EXPECT_FALSE(parse_stations(unterminated));
}

TEST(JsonMappingTest, ToleratesExtremeNumbers) {
  const auto stations = parse_stations(
      R"([{"votes": 1e300, "bitrate": -1e300, "clickcount": 18446744073709551615,
           "clicktrend": -9223372036854775808, "geo_lat": "nan", "geo_long": "inf"}])");
  ASSERT_TRUE(stations) << to_string(stations.error());
  ASSERT_EQ(stations->size(), 1U);
  const Station& station = stations->front();
  EXPECT_EQ(station.votes, std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(station.bitrate, std::numeric_limits<std::int64_t>::min());
  EXPECT_EQ(station.click_count, std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(station.click_trend, std::numeric_limits<std::int64_t>::min());
  EXPECT_FALSE(station.geo_lat.has_value());
  EXPECT_FALSE(station.geo_long.has_value());
}

// Deterministic mutation testing: corrupted versions of every fixture are fed
// into every parser. They may fail, but must never crash or throw.
TEST(JsonMappingTest, MalformedInputNeverCrashes) {
  const std::vector<std::string> fixtures{
      "checks.json", "checksteps.json",       "click.json",     "clicks.json",
      "config.json", "countries.json",        "languages.json", "servers.json",
      "states.json", "station_changes.json",  "stations.json",  "stations_tolerant.json",
      "stats.json",  "streamingservers.json", "tags.json",      "vote.json"};
  const auto parse_all = [](const std::string& body) {
    static_cast<void>(parse_stations(body));
    static_cast<void>(parse_station_changes(body));
    static_cast<void>(parse_countries(body));
    static_cast<void>(parse_languages(body));
    static_cast<void>(parse_states(body));
    static_cast<void>(parse_named_counts(body));
    static_cast<void>(parse_click_result(body));
    static_cast<void>(parse_vote_result(body));
    static_cast<void>(parse_server_stats(body));
    static_cast<void>(parse_server_config(body));
    static_cast<void>(parse_server_mirrors(body));
    static_cast<void>(parse_station_checks(body));
    static_cast<void>(parse_station_clicks(body));
    static_cast<void>(parse_check_steps(body));
    static_cast<void>(parse_streaming_servers(body));
  };

  std::mt19937 engine(20261103U);  // fixed seed: reproducible
  // Includes an embedded NUL byte, hence the explicit length.
  static constexpr char kInteresting[] = "{}[]\",:0-.eE\\ \x00\xff\xc3";
  const std::string interesting(kInteresting, sizeof(kInteresting) - 1);
  for (const auto& name : fixtures) {
    const std::string original = load_fixture(name);
    ASSERT_NO_THROW(parse_all(original)) << name;
    for (int round = 0; round < 150; ++round) {
      std::string mutated = original;
      const int edits = 1 + static_cast<int>(engine() % 4U);
      for (int edit = 0; edit < edits && !mutated.empty(); ++edit) {
        const std::size_t position = engine() % mutated.size();
        switch (engine() % 4U) {
          case 0:  // truncate
            mutated.resize(position);
            break;
          case 1:  // overwrite a byte
            mutated[position] = interesting[engine() % interesting.size()];
            break;
          case 2:  // insert a byte
            mutated.insert(mutated.begin() + static_cast<std::ptrdiff_t>(position),
                           interesting[engine() % interesting.size()]);
            break;
          default:  // duplicate a slice
            mutated.insert(position, mutated.substr(position, engine() % 64U));
            break;
        }
      }
      ASSERT_NO_THROW(parse_all(mutated)) << name << " round " << round;
    }
  }
}

TEST(JsonMappingTest, AcceptsEmptyArray) {
  const auto stations = parse_stations("[]");
  ASSERT_TRUE(stations);
  EXPECT_TRUE(stations->empty());
}

TEST(JsonMappingTest, MapsStationChanges) {
  const auto changes = parse_station_changes(load_fixture("station_changes.json"));
  ASSERT_TRUE(changes);
  ASSERT_EQ(changes->size(), 1U);
  EXPECT_EQ(changes->front().change_uuid, "d1e2f3a4-0000-4000-8000-000000000001");
  EXPECT_EQ(changes->front().votes, 1200);
  EXPECT_EQ(changes->front().tags, (std::vector<std::string>{"jazz"}));
  EXPECT_EQ(epoch(changes->front().last_change_time), 1740823872);
}

TEST(JsonMappingTest, MapsLists) {
  const auto countries = parse_countries(load_fixture("countries.json"));
  ASSERT_TRUE(countries);
  ASSERT_EQ(countries->size(), 2U);
  EXPECT_EQ(countries->at(0).name, "Germany");
  EXPECT_EQ(countries->at(0).iso_3166_1, "DE");
  EXPECT_EQ(countries->at(0).station_count, 4021);
  EXPECT_EQ(countries->at(1).name, "\xC3\x96sterreich");
  EXPECT_EQ(countries->at(1).station_count, 512);

  const auto languages = parse_languages(load_fixture("languages.json"));
  ASSERT_TRUE(languages);
  ASSERT_EQ(languages->size(), 2U);
  EXPECT_EQ(languages->at(0).iso_639, "de");
  EXPECT_TRUE(languages->at(1).iso_639.empty());

  const auto states = parse_states(load_fixture("states.json"));
  ASSERT_TRUE(states);
  ASSERT_EQ(states->size(), 2U);
  EXPECT_EQ(states->at(1).name, "Bayern");
  EXPECT_EQ(states->at(1).country, "Germany");
  EXPECT_EQ(states->at(1).station_count, 330);

  const auto tags = parse_named_counts(load_fixture("tags.json"));
  ASSERT_TRUE(tags);
  ASSERT_EQ(tags->size(), 2U);
  EXPECT_EQ(tags->at(1).name, "smooth jazz");
  EXPECT_EQ(tags->at(1).station_count, 310);
}

TEST(JsonMappingTest, MapsClickAndVote) {
  const auto click = parse_click_result(load_fixture("click.json"));
  ASSERT_TRUE(click);
  EXPECT_TRUE(click->ok);  // "true" as a string
  EXPECT_EQ(click->message, "retrieved station url");
  EXPECT_EQ(click->station_uuid, "960e57c5-0601-11e8-ae97-52543be04c81");
  EXPECT_EQ(click->url, "https://stream.example.com/jazz_128.mp3");

  const auto vote = parse_vote_result(load_fixture("vote.json"));
  ASSERT_TRUE(vote);
  EXPECT_FALSE(vote->ok);
  EXPECT_EQ(vote->message, "you are voting for the same station too often");
}

TEST(JsonMappingTest, MapsServerInformation) {
  const auto stats = parse_server_stats(load_fixture("stats.json"));
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->supported_version, 1);
  EXPECT_EQ(stats->software_version, "0.7.31");
  EXPECT_EQ(stats->status, "OK");
  EXPECT_EQ(stats->stations, 54783);
  EXPECT_EQ(stats->countries, 229);

  const auto config = parse_server_config(load_fixture("config.json"));
  ASSERT_TRUE(config);
  EXPECT_TRUE(config->check_enabled);
  EXPECT_EQ(config->pull_servers.size(), 2U);
  EXPECT_EQ(config->server_name, "de2.api.radio-browser.info");
  EXPECT_EQ(config->check_retries, 5);
  EXPECT_EQ(config->cache_type, "builtin");

  const auto mirrors = parse_server_mirrors(load_fixture("servers.json"));
  ASSERT_TRUE(mirrors);
  ASSERT_EQ(mirrors->size(), 5U);
  EXPECT_EQ(mirrors->at(0).ip, "2a01:4f8:c2c:5e24::1");
  EXPECT_EQ(mirrors->at(0).name, "de1.api.radio-browser.info");
}

TEST(JsonMappingTest, MapsChecksClicksAndSteps) {
  const auto checks = parse_station_checks(load_fixture("checks.json"));
  ASSERT_TRUE(checks);
  ASSERT_EQ(checks->size(), 1U);
  const StationCheck& check = checks->front();
  EXPECT_EQ(check.source, "at1.api.radio-browser.info");
  EXPECT_TRUE(check.ok);
  EXPECT_EQ(epoch(check.timestamp), 1578324898);
  EXPECT_FALSE(check.is_public.has_value());
  ASSERT_TRUE(check.do_not_index);
  EXPECT_TRUE(*check.do_not_index);
  EXPECT_EQ(check.sampling, 44100);
  EXPECT_EQ(check.timing_ms, 125);
  EXPECT_EQ(check.server_software, "Icecast 2.4.0");
  EXPECT_EQ(check.language_codes, (std::vector<std::string>{"de", "en"}));

  const auto clicks = parse_station_clicks(load_fixture("clicks.json"));
  ASSERT_TRUE(clicks);
  ASSERT_EQ(clicks->size(), 1U);
  EXPECT_EQ(clicks->front().click_uuid, "5c1f3b2a-0000-4000-8000-00000000c11c");
  EXPECT_TRUE(clicks->front().click_timestamp.has_value());

  const auto steps = parse_check_steps(load_fixture("checksteps.json"));
  ASSERT_TRUE(steps);
  ASSERT_EQ(steps->size(), 1U);
  EXPECT_TRUE(steps->front().parent_step_uuid.empty());
  EXPECT_EQ(steps->front().url_type, "PLS");
  EXPECT_TRUE(steps->front().creation_time.has_value());

  const auto servers = parse_streaming_servers(load_fixture("streamingservers.json"));
  ASSERT_TRUE(servers);
  ASSERT_EQ(servers->size(), 1U);
  EXPECT_EQ(servers->front().status_url, "https://stream.example.com/status-json.xsl");
  EXPECT_TRUE(servers->front().error.empty());
}

}  // namespace
}  // namespace radiobrowser::detail
