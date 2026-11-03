// SPDX-License-Identifier: MIT
/// @file
/// Data types returned by the radio-browser API.
///
/// The library maps the API's JSON tolerantly: missing or null fields keep
/// their default values, numbers that arrive as strings are converted, and
/// booleans may arrive as true/false, 0/1 or "true"/"false". Text fields are
/// trimmed, and list fields (tags, languages, ...) are split at commas.

#pragma once

#include <radiobrowser/export.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace radiobrowser {

/// A point in time with second precision, in UTC.
using Timestamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>;

/// Parses an API timestamp. Accepts ISO 8601 ("2024-01-31T18:37:02Z", with
/// optional fractional seconds and UTC offset) and the legacy format
/// ("2024-01-31 18:37:02", interpreted as UTC). Returns std::nullopt for
/// empty or malformed input.
[[nodiscard]] RADIOBROWSER_EXPORT std::optional<Timestamp> parse_timestamp(std::string_view text);

/// Formats a timestamp as ISO 8601 in UTC, e.g. "2024-01-31T18:37:02Z".
[[nodiscard]] RADIOBROWSER_EXPORT std::string format_timestamp(Timestamp timestamp);

/// A radio station.
struct Station {
  /// Stable identifier of the station; use it to store favourites.
  std::string station_uuid;
  /// Identifier of the latest change of this station's data.
  std::string change_uuid;
  /// Identifier of the streaming server, empty if unknown.
  std::string server_uuid;
  /// Display name.
  std::string name;
  /// Stream URL as entered by the station's maintainer; may be a playlist.
  std::string url;
  /// Stream URL with playlists resolved. Prefer stream_url() for playback.
  std::string url_resolved;
  /// The station's web site.
  std::string homepage;
  /// URL of the station's icon.
  std::string favicon;
  /// Tags such as "jazz" or "news".
  std::vector<std::string> tags;
  /// Country name. Prefer country_code.
  std::string country;
  /// ISO 3166-1 alpha-2 country code, e.g. "DE".
  std::string country_code;
  /// ISO 3166-2 subdivision code, e.g. "DE-HE".
  std::string iso_3166_2;
  /// Name of the state or region.
  std::string state;
  /// Spoken languages, e.g. "german".
  std::vector<std::string> languages;
  /// ISO 639 language codes, e.g. "de".
  std::vector<std::string> language_codes;
  /// Number of votes.
  std::int64_t votes = 0;
  /// Audio codec, e.g. "MP3" or "AAC".
  std::string codec;
  /// Bitrate in kbit/s; 0 if unknown.
  std::int64_t bitrate = 0;
  /// True if the stream uses HTTP Live Streaming.
  bool hls = false;
  /// True if the latest availability check succeeded.
  bool last_check_ok = false;
  /// Time of the latest change of the station's data.
  std::optional<Timestamp> last_change_time;
  /// Time of the latest availability check.
  std::optional<Timestamp> last_check_time;
  /// Time of the latest successful availability check.
  std::optional<Timestamp> last_check_ok_time;
  /// Time of the latest check performed by the answering server.
  std::optional<Timestamp> last_local_check_time;
  /// Time of the latest click.
  std::optional<Timestamp> click_timestamp;
  /// Number of clicks within the last 24 hours.
  std::int64_t click_count = 0;
  /// Change of the click count compared to the day before.
  std::int64_t click_trend = 0;
  /// True if the latest check failed because of a TLS error.
  bool ssl_error = false;
  /// Latitude in degrees, if known.
  std::optional<double> geo_lat;
  /// Longitude in degrees, if known.
  std::optional<double> geo_long;
  /// True if the stream provides extended information in HTTP headers.
  bool has_extended_info = false;

  /// Returns the URL to play: url_resolved if set, otherwise url.
  [[nodiscard]] const std::string& stream_url() const noexcept {
    return url_resolved.empty() ? url : url_resolved;
  }
};

/// One historic version of a station, see Client::station_changes().
struct StationChange {
  /// Identifier of this change.
  std::string change_uuid;
  /// Identifier of the changed station.
  std::string station_uuid;
  /// Display name.
  std::string name;
  /// Stream URL as entered by the station's maintainer.
  std::string url;
  /// The station's web site.
  std::string homepage;
  /// URL of the station's icon.
  std::string favicon;
  /// Tags such as "jazz" or "news".
  std::vector<std::string> tags;
  /// Country name.
  std::string country;
  /// ISO 3166-1 alpha-2 country code.
  std::string country_code;
  /// Name of the state or region.
  std::string state;
  /// Spoken languages.
  std::vector<std::string> languages;
  /// ISO 639 language codes.
  std::vector<std::string> language_codes;
  /// Number of votes at the time of the change.
  std::int64_t votes = 0;
  /// Time of the change.
  std::optional<Timestamp> last_change_time;
  /// Latitude in degrees, if known.
  std::optional<double> geo_lat;
  /// Longitude in degrees, if known.
  std::optional<double> geo_long;
};

/// A name with the number of stations using it (tags, codecs, country codes).
struct NamedCount {
  /// The tag, codec or country code.
  std::string name;
  /// Number of stations.
  std::int64_t station_count = 0;
};

/// A tag and its number of stations.
using TagCount = NamedCount;
/// A codec and its number of stations.
using CodecCount = NamedCount;
/// A country code and its number of stations.
using CountryCodeCount = NamedCount;

/// A country.
struct Country {
  /// Country name.
  std::string name;
  /// ISO 3166-1 alpha-2 code.
  std::string iso_3166_1;
  /// Number of stations.
  std::int64_t station_count = 0;
};

/// A language.
struct Language {
  /// Language name, e.g. "german".
  std::string name;
  /// ISO 639 code; empty if unknown.
  std::string iso_639;
  /// Number of stations.
  std::int64_t station_count = 0;
};

/// A state or region of a country.
struct State {
  /// Name of the state.
  std::string name;
  /// Name of the country.
  std::string country;
  /// Number of stations.
  std::int64_t station_count = 0;
};

/// The answer to Client::count_click().
struct ClickResult {
  /// Mirrors the station's last_check_ok flag; it does not tell whether the
  /// click was counted (the server never reports that).
  bool ok = false;
  /// Status message of the server.
  std::string message;
  /// Identifier of the clicked station.
  std::string station_uuid;
  /// Display name of the station.
  std::string name;
  /// The resolved stream URL to play.
  std::string url;
};

/// The answer to Client::vote().
struct VoteResult {
  /// True if the vote was counted. Voting for the same station repeatedly
  /// within a short time is rejected by the server.
  bool ok = false;
  /// Status message of the server.
  std::string message;
};

/// Statistics of the answering server.
struct ServerStats {
  /// API version supported by the server.
  std::int64_t supported_version = 0;
  /// Version of the server software.
  std::string software_version;
  /// Server status, "OK" if healthy.
  std::string status;
  /// Number of stations.
  std::int64_t stations = 0;
  /// Number of broken stations.
  std::int64_t stations_broken = 0;
  /// Number of tags.
  std::int64_t tags = 0;
  /// Number of clicks within the last hour.
  std::int64_t clicks_last_hour = 0;
  /// Number of clicks within the last day.
  std::int64_t clicks_last_day = 0;
  /// Number of languages.
  std::int64_t languages = 0;
  /// Number of countries.
  std::int64_t countries = 0;
};

/// Configuration of the answering server, as reported by /json/config.
struct ServerConfig {
  /// Stream checks are enabled.
  bool check_enabled = false;
  /// Metrics export is enabled.
  bool prometheus_exporter_enabled = false;
  /// Servers this one mirrors.
  std::vector<std::string> pull_servers;
  /// TCP timeout of checks.
  std::int64_t tcp_timeout_seconds = 0;
  /// Removal delay of stations that never worked.
  std::int64_t broken_stations_never_working_timeout_seconds = 0;
  /// Removal delay of broken stations.
  std::int64_t broken_stations_timeout_seconds = 0;
  /// Retention of check results.
  std::int64_t checks_timeout_seconds = 0;
  /// Minimum time between clicks.
  std::int64_t click_valid_timeout_seconds = 0;
  /// Retention of clicks.
  std::int64_t clicks_timeout_seconds = 0;
  /// Mirror synchronisation interval.
  std::int64_t mirror_pull_interval_seconds = 0;
  /// Cache update interval.
  std::int64_t update_caches_interval_seconds = 0;
  /// Host name of the server.
  std::string server_name;
  /// Location of the server.
  std::string server_location;
  /// Country code of the server.
  std::string server_country_code;
  /// Retries per check.
  std::int64_t check_retries = 0;
  /// Stations per check batch.
  std::int64_t check_batchsize = 0;
  /// Pause between check batches.
  std::int64_t check_pause_seconds = 0;
  /// Worker threads of the API.
  std::int64_t api_threads = 0;
  /// Response cache type.
  std::string cache_type;
  /// Response cache lifetime.
  std::int64_t cache_ttl = 0;
  /// Language replacement table.
  std::string language_replace_filepath;
  /// Language code table.
  std::string language_to_code_filepath;
};

/// An API server as reported by /json/servers.
struct ServerMirror {
  /// IP address of the server.
  std::string ip;
  /// Host name, e.g. "de1.api.radio-browser.info".
  std::string name;
};

/// The result of one availability check of a station.
struct StationCheck {
  /// Identifier of the checked station.
  std::string station_uuid;
  /// Identifier of this check.
  std::string check_uuid;
  /// Host name of the server that performed the check.
  std::string source;
  /// Detected audio codec.
  std::string codec;
  /// Detected bitrate in kbit/s.
  std::int64_t bitrate = 0;
  /// True if the stream uses HTTP Live Streaming.
  bool hls = false;
  /// True if the stream was reachable and playable.
  bool ok = false;
  /// Time of the check.
  std::optional<Timestamp> timestamp;
  /// The resolved stream URL found by the check.
  std::string url_cache;
  /// True if information sent by the stream overrides the database.
  bool metainfo_overrides_database = false;
  /// Whether the stream declares itself public, if it said so.
  std::optional<bool> is_public;
  /// Name reported by the stream.
  std::string name;
  /// Description reported by the stream.
  std::string description;
  /// Tags reported by the stream.
  std::vector<std::string> tags;
  /// Country code reported by the stream.
  std::string country_code;
  /// Subdivision code reported by the stream.
  std::string country_subdivision_code;
  /// Web site reported by the stream.
  std::string homepage;
  /// Icon URL reported by the stream.
  std::string favicon;
  /// Load balancer URL reported by the stream.
  std::string loadbalancer;
  /// Whether the stream asks not to be indexed, if it said so.
  std::optional<bool> do_not_index;
  /// Streaming server software, e.g. "Icecast 2.4.0".
  std::string server_software;
  /// Sampling rate in Hz, if known.
  std::optional<std::int64_t> sampling;
  /// Duration of the check in milliseconds, if known.
  std::optional<std::int64_t> timing_ms;
  /// Language codes reported by the stream.
  std::vector<std::string> language_codes;
  /// True if the check failed because of a TLS error.
  bool ssl_error = false;
  /// Latitude reported by the stream, if any.
  std::optional<double> geo_lat;
  /// Longitude reported by the stream, if any.
  std::optional<double> geo_long;
};

/// A single click on a station.
struct StationClick {
  /// Identifier of the clicked station.
  std::string station_uuid;
  /// Identifier of this click.
  std::string click_uuid;
  /// Time of the click.
  std::optional<Timestamp> click_timestamp;
};

/// One step of a station check, e.g. following a playlist.
struct StationCheckStep {
  /// Identifier of this step.
  std::string step_uuid;
  /// Identifier of the previous step; empty for the first step.
  std::string parent_step_uuid;
  /// Identifier of the check this step belongs to.
  std::string check_uuid;
  /// Identifier of the checked station.
  std::string station_uuid;
  /// URL examined in this step.
  std::string url;
  /// Detected type of the URL, e.g. "PLS" or "STREAM".
  std::string url_type;
  /// Error message if the step failed.
  std::string error;
  /// Time the step was recorded.
  std::optional<Timestamp> creation_time;
};

/// A streaming server known to the API.
struct StreamingServer {
  /// Identifier of the streaming server.
  std::string uuid;
  /// Base URL of the streaming server.
  std::string url;
  /// URL of the server's status page.
  std::string status_url;
  /// Status reported by the server.
  std::string status;
  /// Error message of the latest status request.
  std::string error;
};

}  // namespace radiobrowser
