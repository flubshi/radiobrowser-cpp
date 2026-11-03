// SPDX-License-Identifier: MIT
/// @file
/// Query parameters for the Client calls.
///
/// Every query is a plain aggregate: default construct it and set the fields
/// you need. Fields left at their defaults are not sent to the server.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace radiobrowser {

/// Sort order of station lists.
enum class StationOrder : std::uint8_t {
  Name,             ///< By name.
  Url,              ///< By stream URL.
  Homepage,         ///< By web site.
  Favicon,          ///< By icon URL.
  Tags,             ///< By tags.
  Country,          ///< By country name.
  State,            ///< By state name.
  Language,         ///< By language.
  Votes,            ///< By number of votes.
  Codec,            ///< By codec.
  Bitrate,          ///< By bitrate.
  LastCheckOk,      ///< By result of the latest check.
  LastCheckTime,    ///< By time of the latest check.
  ClickTimestamp,   ///< By time of the latest click.
  ClickCount,       ///< By number of clicks within the last day.
  ClickTrend,       ///< By change of the click count.
  ChangeTimestamp,  ///< By time of the latest change.
  Random,           ///< In random order.
};

/// Sort order of countries, languages, tags, codecs and states.
enum class ListOrder : std::uint8_t {
  Name,          ///< By name.
  StationCount,  ///< By number of stations.
};

/// The station attribute used by Client::stations_by().
enum class StationField : std::uint8_t {
  Name,         ///< Station name.
  Codec,        ///< Codec.
  Country,      ///< Country name.
  CountryCode,  ///< ISO 3166-1 alpha-2 country code; always matched exactly.
  State,        ///< State or region.
  Language,     ///< Language.
  Tag,          ///< Tag.
};

/// Parameters for lists of countries, languages, tags, codecs and country codes.
struct ListQuery {
  /// Only return entries whose name contains this text. Empty means all.
  std::string filter;
  /// Sort order.
  ListOrder order = ListOrder::Name;
  /// Sort in descending order.
  bool reverse = false;
  /// Ignore broken stations when counting. Defaults to ClientOptions::hide_broken.
  std::optional<bool> hide_broken;
  /// Number of entries to skip.
  std::size_t offset = 0;
  /// Maximum number of entries. Unlimited if not set.
  std::optional<std::size_t> limit;
};

/// Parameters for Client::states().
struct StateQuery {
  /// Only return states of this country (country name, not code). Empty means all.
  std::string country;
  /// Only return states whose name contains this text. Empty means all.
  std::string filter;
  /// Sort order.
  ListOrder order = ListOrder::Name;
  /// Sort in descending order.
  bool reverse = false;
  /// Ignore broken stations when counting. Defaults to ClientOptions::hide_broken.
  std::optional<bool> hide_broken;
  /// Number of entries to skip.
  std::size_t offset = 0;
  /// Maximum number of entries. Unlimited if not set.
  std::optional<std::size_t> limit;
};

/// Sorting and paging for station lists.
struct StationListQuery {
  /// Sort order.
  StationOrder order = StationOrder::Name;
  /// Sort in descending order.
  bool reverse = false;
  /// Skip stations that failed their latest check. Defaults to ClientOptions::hide_broken.
  std::optional<bool> hide_broken;
  /// Number of stations to skip.
  std::size_t offset = 0;
  /// Maximum number of stations. Defaults to ClientOptions::default_limit.
  std::optional<std::size_t> limit;
};

/// Paging for lists with a fixed order (top clicked, top voted, ...).
struct PageQuery {
  /// Skip stations that failed their latest check. Defaults to ClientOptions::hide_broken.
  std::optional<bool> hide_broken;
  /// Number of entries to skip.
  std::size_t offset = 0;
  /// Maximum number of entries. Defaults to ClientOptions::default_limit.
  std::optional<std::size_t> limit;
};

/// Parameters for the advanced station search (/json/stations/search).
///
/// All criteria are combined; text criteria match substrings unless the
/// corresponding *_exact flag is set.
struct StationSearch {
  /// Station name.
  std::string name;
  /// Match the name exactly.
  bool name_exact = false;
  /// Country name.
  std::string country;
  /// Match the country name exactly.
  bool country_exact = false;
  /// ISO 3166-1 alpha-2 country code, e.g. "DE".
  std::string country_code;
  /// State or region.
  std::string state;
  /// Match the state exactly.
  bool state_exact = false;
  /// Language, e.g. "german".
  std::string language;
  /// Match the language exactly.
  bool language_exact = false;
  /// Tag, e.g. "jazz".
  std::string tag;
  /// Match the tag exactly.
  bool tag_exact = false;
  /// Stations must have all of these tags (exact match).
  std::vector<std::string> tag_list;
  /// Codec, e.g. "MP3".
  std::string codec;
  /// Minimum bitrate in kbit/s.
  std::optional<std::uint32_t> bitrate_min;
  /// Maximum bitrate in kbit/s.
  std::optional<std::uint32_t> bitrate_max;
  /// Only stations with (true) or without (false) geographic coordinates.
  std::optional<bool> has_geo_info;
  /// Only stations with (true) or without (false) extended information.
  std::optional<bool> has_extended_info;
  /// Only stations with HTTPS (true) or HTTP (false) stream URLs.
  std::optional<bool> is_https;
  /// Sort order.
  StationOrder order = StationOrder::Name;
  /// Sort in descending order.
  bool reverse = false;
  /// Skip stations that failed their latest check. Defaults to ClientOptions::hide_broken.
  std::optional<bool> hide_broken;
  /// Number of stations to skip.
  std::size_t offset = 0;
  /// Maximum number of stations. Defaults to ClientOptions::default_limit.
  std::optional<std::size_t> limit;
};

/// Parameters for Client::station_changes().
struct ChangesQuery {
  /// Only changes of this station. Empty means all stations.
  std::string station_uuid;
  /// Only changes after this change uuid; use it to poll incrementally.
  std::string last_change_uuid;
  /// Maximum number of changes. Defaults to ClientOptions::default_limit.
  std::optional<std::size_t> limit;
};

/// Parameters for Client::checks().
struct ChecksQuery {
  /// Only checks of this station. Empty means all stations.
  std::string station_uuid;
  /// Only checks after this check uuid; use it to poll incrementally.
  std::string last_check_uuid;
  /// Only checks of the last N seconds.
  std::optional<std::chrono::seconds> seconds;
  /// Maximum number of checks. Defaults to ClientOptions::default_limit.
  std::optional<std::size_t> limit;
};

/// Parameters for Client::clicks().
struct ClicksQuery {
  /// Only clicks of this station. Empty means all stations.
  std::string station_uuid;
  /// Only clicks after this click uuid; use it to poll incrementally.
  std::string last_click_uuid;
  /// Only clicks of the last N seconds.
  std::optional<std::chrono::seconds> seconds;
};

}  // namespace radiobrowser
