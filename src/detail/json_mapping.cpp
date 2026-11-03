// SPDX-License-Identifier: MIT

#include "detail/json_mapping.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace radiobrowser::detail {

namespace {

using Json = nlohmann::json;

[[nodiscard]] constexpr bool is_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
  while (!text.empty() && is_space(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_space(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] std::string to_lower(std::string_view text) {
  std::string lower(text);
  for (char& c : lower) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return lower;
}

// Locale independent string to double conversion.
[[nodiscard]] std::optional<double> parse_double(std::string_view text) {
  text = trim(text);
  if (text.empty()) {
    return std::nullopt;
  }
  std::istringstream stream{std::string(text)};
  stream.imbue(std::locale::classic());
  double value = 0.0;
  stream >> value;
  if (stream.fail() || !stream.eof() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::int64_t clamp_to_int64(double value) noexcept {
  constexpr auto kMax = static_cast<double>(std::numeric_limits<std::int64_t>::max());
  constexpr auto kMin = static_cast<double>(std::numeric_limits<std::int64_t>::min());
  if (value >= kMax) {
    return std::numeric_limits<std::int64_t>::max();
  }
  if (value <= kMin) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return static_cast<std::int64_t>(value);
}

[[nodiscard]] std::optional<std::int64_t> parse_int(std::string_view text) {
  text = trim(text);
  if (text.empty()) {
    return std::nullopt;
  }
  std::int64_t value = 0;
  const char* const begin = text.data();
  const char* const end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec == std::errc() && ptr == end) {
    return value;
  }
  // Numbers such as "128.0" or "1e3".
  if (const auto as_double = parse_double(text)) {
    return clamp_to_int64(*as_double);
  }
  return std::nullopt;
}

[[nodiscard]] const Json* find_field(const Json& object, const char* key) {
  const auto it = object.find(key);
  if (it == object.end() || it->is_null()) {
    return nullptr;
  }
  return &*it;
}

[[nodiscard]] std::optional<std::int64_t> to_int(const Json& value) {
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    constexpr auto kMax = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    return number > kMax ? std::numeric_limits<std::int64_t>::max()
                         : static_cast<std::int64_t>(number);
  }
  if (value.is_number_integer()) {
    return value.get<std::int64_t>();
  }
  if (value.is_number_float()) {
    const auto number = value.get<double>();
    if (!std::isfinite(number)) {
      return std::nullopt;
    }
    return clamp_to_int64(number);
  }
  if (value.is_boolean()) {
    return value.get<bool>() ? 1 : 0;
  }
  if (value.is_string()) {
    return parse_int(value.get_ref<const std::string&>());
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<double> to_double(const Json& value) {
  if (value.is_number()) {
    const auto number = value.get<double>();
    return std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
  }
  if (value.is_string()) {
    return parse_double(value.get_ref<const std::string&>());
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<bool> to_bool(const Json& value) {
  if (value.is_boolean()) {
    return value.get<bool>();
  }
  if (value.is_number_integer() || value.is_number_unsigned()) {
    return value.get<std::int64_t>() != 0;
  }
  if (value.is_number_float()) {
    return value.get<double>() != 0.0;
  }
  if (value.is_string()) {
    const std::string text = to_lower(trim(value.get_ref<const std::string&>()));
    if (text == "true" || text == "1" || text == "yes" || text == "on") {
      return true;
    }
    if (text == "false" || text == "0" || text == "no" || text == "off") {
      return false;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::string get_string(const Json& object, const char* key) {
  const Json* value = find_field(object, key);
  if (value == nullptr) {
    return {};
  }
  if (value->is_string()) {
    return std::string(trim(value->get_ref<const std::string&>()));
  }
  if (value->is_boolean()) {
    return value->get<bool>() ? "true" : "false";
  }
  if (value->is_number()) {
    return value->dump();
  }
  return {};
}

[[nodiscard]] std::int64_t get_int(const Json& object, const char* key) {
  const Json* value = find_field(object, key);
  return value == nullptr ? 0 : to_int(*value).value_or(0);
}

[[nodiscard]] std::optional<std::int64_t> get_optional_int(const Json& object, const char* key) {
  const Json* value = find_field(object, key);
  return value == nullptr ? std::nullopt : to_int(*value);
}

[[nodiscard]] bool get_bool(const Json& object, const char* key) {
  const Json* value = find_field(object, key);
  return value == nullptr ? false : to_bool(*value).value_or(false);
}

[[nodiscard]] std::optional<bool> get_optional_bool(const Json& object, const char* key) {
  const Json* value = find_field(object, key);
  return value == nullptr ? std::nullopt : to_bool(*value);
}

[[nodiscard]] std::optional<double> get_optional_double(const Json& object, const char* key) {
  const Json* value = find_field(object, key);
  return value == nullptr ? std::nullopt : to_double(*value);
}

void append_list_item(std::vector<std::string>& list, std::string_view item) {
  item = trim(item);
  if (!item.empty()) {
    list.emplace_back(item);
  }
}

// Accepts "a,b,c" as well as ["a","b","c"].
[[nodiscard]] std::vector<std::string> get_list(const Json& object, const char* key) {
  std::vector<std::string> list;
  const Json* value = find_field(object, key);
  if (value == nullptr) {
    return list;
  }
  if (value->is_string()) {
    std::string_view text = value->get_ref<const std::string&>();
    while (!text.empty()) {
      const auto comma = text.find(',');
      append_list_item(list, text.substr(0, comma));
      if (comma == std::string_view::npos) {
        break;
      }
      text.remove_prefix(comma + 1);
    }
  } else if (value->is_array()) {
    for (const auto& item : *value) {
      if (item.is_string()) {
        append_list_item(list, item.get_ref<const std::string&>());
      }
    }
  }
  return list;
}

// Prefers the ISO 8601 field and falls back to the legacy field.
[[nodiscard]] std::optional<Timestamp> get_time(const Json& object, const char* iso_key,
                                                const char* legacy_key = nullptr) {
  if (const Json* value = find_field(object, iso_key); value != nullptr && value->is_string()) {
    if (auto parsed = parse_timestamp(value->get_ref<const std::string&>())) {
      return parsed;
    }
  }
  if (legacy_key != nullptr) {
    if (const Json* value = find_field(object, legacy_key);
        value != nullptr && value->is_string()) {
      return parse_timestamp(value->get_ref<const std::string&>());
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Json> parse_document(std::string_view body) {
  Json document = Json::parse(body.begin(), body.end(), nullptr, /*allow_exceptions=*/false);
  if (document.is_discarded()) {
    return std::nullopt;
  }
  return document;
}

[[nodiscard]] Error invalid_response(std::string message) {
  return Error{ErrorCode::InvalidResponse, std::move(message), 0};
}

template <typename T, typename Mapper>
[[nodiscard]] Result<std::vector<T>> parse_array(std::string_view body, Mapper map_item) {
  try {
    const auto document = parse_document(body);
    if (!document) {
      return invalid_response("response is not valid JSON");
    }
    if (!document->is_array()) {
      return invalid_response("expected a JSON array");
    }
    std::vector<T> items;
    items.reserve(document->size());
    for (const auto& element : *document) {
      if (element.is_object()) {
        items.push_back(map_item(element));
      }
    }
    return items;
  } catch (const std::exception& e) {
    return invalid_response(std::string("failed to map response: ") + e.what());
  }
}

template <typename T, typename Mapper>
[[nodiscard]] Result<T> parse_object(std::string_view body, Mapper map_object) {
  try {
    const auto document = parse_document(body);
    if (!document) {
      return invalid_response("response is not valid JSON");
    }
    if (!document->is_object()) {
      return invalid_response("expected a JSON object");
    }
    return map_object(*document);
  } catch (const std::exception& e) {
    return invalid_response(std::string("failed to map response: ") + e.what());
  }
}

[[nodiscard]] Station map_station(const Json& object) {
  Station station;
  station.station_uuid = get_string(object, "stationuuid");
  station.change_uuid = get_string(object, "changeuuid");
  station.server_uuid = get_string(object, "serveruuid");
  station.name = get_string(object, "name");
  station.url = get_string(object, "url");
  station.url_resolved = get_string(object, "url_resolved");
  station.homepage = get_string(object, "homepage");
  station.favicon = get_string(object, "favicon");
  station.tags = get_list(object, "tags");
  station.country = get_string(object, "country");
  station.country_code = get_string(object, "countrycode");
  station.iso_3166_2 = get_string(object, "iso_3166_2");
  station.state = get_string(object, "state");
  station.languages = get_list(object, "language");
  station.language_codes = get_list(object, "languagecodes");
  station.votes = get_int(object, "votes");
  station.codec = get_string(object, "codec");
  station.bitrate = get_int(object, "bitrate");
  station.hls = get_bool(object, "hls");
  station.last_check_ok = get_bool(object, "lastcheckok");
  station.last_change_time = get_time(object, "lastchangetime_iso8601", "lastchangetime");
  station.last_check_time = get_time(object, "lastchecktime_iso8601", "lastchecktime");
  station.last_check_ok_time = get_time(object, "lastcheckoktime_iso8601", "lastcheckoktime");
  station.last_local_check_time =
      get_time(object, "lastlocalchecktime_iso8601", "lastlocalchecktime");
  station.click_timestamp = get_time(object, "clicktimestamp_iso8601", "clicktimestamp");
  station.click_count = get_int(object, "clickcount");
  station.click_trend = get_int(object, "clicktrend");
  station.ssl_error = get_bool(object, "ssl_error");
  station.geo_lat = get_optional_double(object, "geo_lat");
  station.geo_long = get_optional_double(object, "geo_long");
  station.has_extended_info = get_bool(object, "has_extended_info");
  return station;
}

[[nodiscard]] StationChange map_station_change(const Json& object) {
  StationChange change;
  change.change_uuid = get_string(object, "changeuuid");
  change.station_uuid = get_string(object, "stationuuid");
  change.name = get_string(object, "name");
  change.url = get_string(object, "url");
  change.homepage = get_string(object, "homepage");
  change.favicon = get_string(object, "favicon");
  change.tags = get_list(object, "tags");
  change.country = get_string(object, "country");
  change.country_code = get_string(object, "countrycode");
  change.state = get_string(object, "state");
  change.languages = get_list(object, "language");
  change.language_codes = get_list(object, "languagecodes");
  change.votes = get_int(object, "votes");
  change.last_change_time = get_time(object, "lastchangetime_iso8601", "lastchangetime");
  change.geo_lat = get_optional_double(object, "geo_lat");
  change.geo_long = get_optional_double(object, "geo_long");
  return change;
}

[[nodiscard]] StationCheck map_station_check(const Json& object) {
  StationCheck check;
  check.station_uuid = get_string(object, "stationuuid");
  check.check_uuid = get_string(object, "checkuuid");
  check.source = get_string(object, "source");
  check.codec = get_string(object, "codec");
  check.bitrate = get_int(object, "bitrate");
  check.hls = get_bool(object, "hls");
  check.ok = get_bool(object, "ok");
  check.timestamp = get_time(object, "timestamp_iso8601", "timestamp");
  check.url_cache = get_string(object, "urlcache");
  check.metainfo_overrides_database = get_bool(object, "metainfo_overrides_database");
  check.is_public = get_optional_bool(object, "public");
  check.name = get_string(object, "name");
  check.description = get_string(object, "description");
  check.tags = get_list(object, "tags");
  check.country_code = get_string(object, "countrycode");
  check.country_subdivision_code = get_string(object, "countrysubdivisioncode");
  check.homepage = get_string(object, "homepage");
  check.favicon = get_string(object, "favicon");
  check.loadbalancer = get_string(object, "loadbalancer");
  check.do_not_index = get_optional_bool(object, "do_not_index");
  check.server_software = get_string(object, "server_software");
  check.sampling = get_optional_int(object, "sampling");
  check.timing_ms = get_optional_int(object, "timing_ms");
  check.language_codes = get_list(object, "languagecodes");
  check.ssl_error = get_bool(object, "ssl_error");
  check.geo_lat = get_optional_double(object, "geo_lat");
  check.geo_long = get_optional_double(object, "geo_long");
  return check;
}

}  // namespace

Result<std::vector<Station>> parse_stations(std::string_view body) {
  return parse_array<Station>(body, map_station);
}

Result<std::vector<StationChange>> parse_station_changes(std::string_view body) {
  return parse_array<StationChange>(body, map_station_change);
}

Result<std::vector<Country>> parse_countries(std::string_view body) {
  return parse_array<Country>(body, [](const Json& object) {
    return Country{get_string(object, "name"), get_string(object, "iso_3166_1"),
                   get_int(object, "stationcount")};
  });
}

Result<std::vector<Language>> parse_languages(std::string_view body) {
  return parse_array<Language>(body, [](const Json& object) {
    return Language{get_string(object, "name"), get_string(object, "iso_639"),
                    get_int(object, "stationcount")};
  });
}

Result<std::vector<State>> parse_states(std::string_view body) {
  return parse_array<State>(body, [](const Json& object) {
    return State{get_string(object, "name"), get_string(object, "country"),
                 get_int(object, "stationcount")};
  });
}

Result<std::vector<NamedCount>> parse_named_counts(std::string_view body) {
  return parse_array<NamedCount>(body, [](const Json& object) {
    return NamedCount{get_string(object, "name"), get_int(object, "stationcount")};
  });
}

Result<ClickResult> parse_click_result(std::string_view body) {
  return parse_object<ClickResult>(body, [](const Json& object) {
    return ClickResult{get_bool(object, "ok"), get_string(object, "message"),
                       get_string(object, "stationuuid"), get_string(object, "name"),
                       get_string(object, "url")};
  });
}

Result<VoteResult> parse_vote_result(std::string_view body) {
  return parse_object<VoteResult>(body, [](const Json& object) {
    return VoteResult{get_bool(object, "ok"), get_string(object, "message")};
  });
}

Result<ServerStats> parse_server_stats(std::string_view body) {
  return parse_object<ServerStats>(body, [](const Json& object) {
    ServerStats stats;
    stats.supported_version = get_int(object, "supported_version");
    stats.software_version = get_string(object, "software_version");
    stats.status = get_string(object, "status");
    stats.stations = get_int(object, "stations");
    stats.stations_broken = get_int(object, "stations_broken");
    stats.tags = get_int(object, "tags");
    stats.clicks_last_hour = get_int(object, "clicks_last_hour");
    stats.clicks_last_day = get_int(object, "clicks_last_day");
    stats.languages = get_int(object, "languages");
    stats.countries = get_int(object, "countries");
    return stats;
  });
}

Result<ServerConfig> parse_server_config(std::string_view body) {
  return parse_object<ServerConfig>(body, [](const Json& object) {
    ServerConfig config;
    config.check_enabled = get_bool(object, "check_enabled");
    config.prometheus_exporter_enabled = get_bool(object, "prometheus_exporter_enabled");
    config.pull_servers = get_list(object, "pull_servers");
    config.tcp_timeout_seconds = get_int(object, "tcp_timeout_seconds");
    config.broken_stations_never_working_timeout_seconds =
        get_int(object, "broken_stations_never_working_timeout_seconds");
    config.broken_stations_timeout_seconds = get_int(object, "broken_stations_timeout_seconds");
    config.checks_timeout_seconds = get_int(object, "checks_timeout_seconds");
    config.click_valid_timeout_seconds = get_int(object, "click_valid_timeout_seconds");
    config.clicks_timeout_seconds = get_int(object, "clicks_timeout_seconds");
    config.mirror_pull_interval_seconds = get_int(object, "mirror_pull_interval_seconds");
    config.update_caches_interval_seconds = get_int(object, "update_caches_interval_seconds");
    config.server_name = get_string(object, "server_name");
    config.server_location = get_string(object, "server_location");
    config.server_country_code = get_string(object, "server_country_code");
    config.check_retries = get_int(object, "check_retries");
    config.check_batchsize = get_int(object, "check_batchsize");
    config.check_pause_seconds = get_int(object, "check_pause_seconds");
    config.api_threads = get_int(object, "api_threads");
    config.cache_type = get_string(object, "cache_type");
    config.cache_ttl = get_int(object, "cache_ttl");
    config.language_replace_filepath = get_string(object, "language_replace_filepath");
    config.language_to_code_filepath = get_string(object, "language_to_code_filepath");
    return config;
  });
}

Result<std::vector<ServerMirror>> parse_server_mirrors(std::string_view body) {
  return parse_array<ServerMirror>(body, [](const Json& object) {
    return ServerMirror{get_string(object, "ip"), get_string(object, "name")};
  });
}

Result<std::vector<StationCheck>> parse_station_checks(std::string_view body) {
  return parse_array<StationCheck>(body, map_station_check);
}

Result<std::vector<StationClick>> parse_station_clicks(std::string_view body) {
  return parse_array<StationClick>(body, [](const Json& object) {
    return StationClick{get_string(object, "stationuuid"), get_string(object, "clickuuid"),
                        get_time(object, "clicktimestamp_iso8601", "clicktimestamp")};
  });
}

Result<std::vector<StationCheckStep>> parse_check_steps(std::string_view body) {
  return parse_array<StationCheckStep>(body, [](const Json& object) {
    StationCheckStep step;
    step.step_uuid = get_string(object, "stepuuid");
    step.parent_step_uuid = get_string(object, "parent_stepuuid");
    step.check_uuid = get_string(object, "checkuuid");
    step.station_uuid = get_string(object, "stationuuid");
    step.url = get_string(object, "url");
    step.url_type = get_string(object, "urltype");
    step.error = get_string(object, "error");
    step.creation_time = get_time(object, "creation_iso8601");
    return step;
  });
}

Result<std::vector<StreamingServer>> parse_streaming_servers(std::string_view body) {
  return parse_array<StreamingServer>(body, [](const Json& object) {
    return StreamingServer{get_string(object, "uuid"), get_string(object, "url"),
                           get_string(object, "statusurl"), get_string(object, "status"),
                           get_string(object, "error")};
  });
}

}  // namespace radiobrowser::detail
