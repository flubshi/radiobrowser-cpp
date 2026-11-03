// SPDX-License-Identifier: MIT
// Internal: maps API JSON responses to the public data types.
//
// The mapping is deliberately tolerant because the API's data is user
// maintained and field types have changed over time: missing and null
// fields keep their defaults, numbers may arrive as strings, booleans as
// numbers or strings, and list fields as comma separated strings or arrays.
// Array elements that are not objects are skipped.

#pragma once

#include <radiobrowser/result.hpp>
#include <radiobrowser/types.hpp>

#include <string_view>
#include <vector>

namespace radiobrowser::detail {

[[nodiscard]] Result<std::vector<Station>> parse_stations(std::string_view body);
[[nodiscard]] Result<std::vector<StationChange>> parse_station_changes(std::string_view body);
[[nodiscard]] Result<std::vector<Country>> parse_countries(std::string_view body);
[[nodiscard]] Result<std::vector<Language>> parse_languages(std::string_view body);
[[nodiscard]] Result<std::vector<State>> parse_states(std::string_view body);
[[nodiscard]] Result<std::vector<NamedCount>> parse_named_counts(std::string_view body);
[[nodiscard]] Result<ClickResult> parse_click_result(std::string_view body);
[[nodiscard]] Result<VoteResult> parse_vote_result(std::string_view body);
[[nodiscard]] Result<ServerStats> parse_server_stats(std::string_view body);
[[nodiscard]] Result<ServerConfig> parse_server_config(std::string_view body);
[[nodiscard]] Result<std::vector<ServerMirror>> parse_server_mirrors(std::string_view body);
[[nodiscard]] Result<std::vector<StationCheck>> parse_station_checks(std::string_view body);
[[nodiscard]] Result<std::vector<StationClick>> parse_station_clicks(std::string_view body);
[[nodiscard]] Result<std::vector<StationCheckStep>> parse_check_steps(std::string_view body);
[[nodiscard]] Result<std::vector<StreamingServer>> parse_streaming_servers(std::string_view body);

}  // namespace radiobrowser::detail
