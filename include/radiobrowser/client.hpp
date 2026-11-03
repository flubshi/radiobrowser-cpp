// SPDX-License-Identifier: MIT
/// @file
/// The radio-browser API client.

#pragma once

#include <radiobrowser/cancellation.hpp>
#include <radiobrowser/discovery.hpp>
#include <radiobrowser/export.hpp>
#include <radiobrowser/http.hpp>
#include <radiobrowser/log.hpp>
#include <radiobrowser/query.hpp>
#include <radiobrowser/result.hpp>
#include <radiobrowser/types.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
// Private STL members of exported classes are not part of the ABI.
#pragma warning(disable : 4251)
#endif

namespace radiobrowser {

/// Configuration of a Client.
struct ClientOptions {
  /// Identifies your application to the API operators, e.g. "myplayer/1.4".
  /// Empty means "radiobrowser-cpp/<version>". Please set a speaking value.
  std::string user_agent;
  /// Performs HTTP requests. Empty means the built-in libcurl transport if the
  /// library was built with RADIOBROWSER_WITH_CURL; otherwise it is required.
  std::shared_ptr<HttpTransport> transport;
  /// Finds API servers. Empty means make_default_discovery().
  std::shared_ptr<ServerDiscovery> discovery;
  /// Total time budget of one call including discovery and all failover
  /// attempts.
  std::chrono::milliseconds timeout{20000};
  /// Time budget of a single attempt against one server.
  std::chrono::milliseconds attempt_timeout{8000};
  /// Time budget for establishing a connection.
  std::chrono::milliseconds connect_timeout{5000};
  /// How long a discovered server list is reused.
  std::chrono::seconds server_list_ttl{3600};
  /// Randomise the server order after discovery to spread the load, as the
  /// API documentation asks for. Disable only for tests.
  bool shuffle_servers = true;
  /// Limit used for station lists when a query does not set one.
  std::size_t default_limit = 100;
  /// Default for the hide_broken flag of all queries.
  bool hide_broken = true;
  /// Responses larger than this are rejected with ErrorCode::ResponseTooLarge.
  std::size_t max_response_bytes = std::size_t{32} * 1024U * 1024U;
  /// Receives log messages. Empty means silent.
  LogCallback logger;
};

/// Per-call options.
struct RequestOptions {
  /// Overrides ClientOptions::timeout for this call.
  std::optional<std::chrono::milliseconds> timeout;
  /// Cancels the call when triggered.
  CancellationToken cancellation;
};

/// A thread-safe client for the radio-browser API.
///
/// All member functions are const and may be called concurrently from any
/// number of threads. They never throw; failures are reported as Error.
/// Every call is bounded by a deadline (ClientOptions::timeout or
/// RequestOptions::timeout) that covers discovery and all failover attempts.
class RADIOBROWSER_EXPORT Client {
 public:
  /// Creates a client. Fails with ErrorCode::InvalidArgument for invalid options.
  [[nodiscard]] static Result<Client> create(ClientOptions options = {});

  /// Moves the client. The moved-from client reports ErrorCode::Internal.
  Client(Client&& other) noexcept;
  /// Moves the client. The moved-from client reports ErrorCode::Internal.
  Client& operator=(Client&& other) noexcept;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  ~Client();

  /// The User-Agent sent with every request.
  [[nodiscard]] const std::string& user_agent() const noexcept;

  // --- Servers -------------------------------------------------------------

  /// Returns the base URLs of the known servers, running discovery if the
  /// cached list is empty or expired.
  [[nodiscard]] Result<std::vector<std::string>> servers(const RequestOptions& options = {}) const;

  /// Runs discovery now and replaces the cached server list.
  [[nodiscard]] Result<std::vector<std::string>> refresh_servers(
      const RequestOptions& options = {}) const;

  /// Lists the API servers as reported by the server network (/json/servers).
  [[nodiscard]] Result<std::vector<ServerMirror>> server_mirrors(
      const RequestOptions& options = {}) const;

  /// Statistics of the answering server (/json/stats).
  [[nodiscard]] Result<ServerStats> stats(const RequestOptions& options = {}) const;

  /// Configuration of the answering server (/json/config).
  [[nodiscard]] Result<ServerConfig> config(const RequestOptions& options = {}) const;

  // --- Lists ---------------------------------------------------------------

  /// Countries with their number of stations (/json/countries).
  [[nodiscard]] Result<std::vector<Country>> countries(const ListQuery& query = {},
                                                       const RequestOptions& options = {}) const;

  /// Country codes with their number of stations (/json/countrycodes).
  [[nodiscard]] Result<std::vector<CountryCodeCount>> country_codes(
      const ListQuery& query = {}, const RequestOptions& options = {}) const;

  /// Codecs with their number of stations (/json/codecs).
  [[nodiscard]] Result<std::vector<CodecCount>> codecs(const ListQuery& query = {},
                                                       const RequestOptions& options = {}) const;

  /// States with their number of stations (/json/states).
  [[nodiscard]] Result<std::vector<State>> states(const StateQuery& query = {},
                                                  const RequestOptions& options = {}) const;

  /// Languages with their number of stations (/json/languages).
  [[nodiscard]] Result<std::vector<Language>> languages(const ListQuery& query = {},
                                                        const RequestOptions& options = {}) const;

  /// Tags with their number of stations (/json/tags). The full tag list is
  /// large; consider setting a filter or a limit.
  [[nodiscard]] Result<std::vector<TagCount>> tags(const ListQuery& query = {},
                                                   const RequestOptions& options = {}) const;

  // --- Stations ------------------------------------------------------------

  /// All stations, page by page (/json/stations).
  [[nodiscard]] Result<std::vector<Station>> stations(const StationListQuery& query = {},
                                                      const RequestOptions& options = {}) const;

  /// Stations whose field matches the term (/json/stations/by{field}[exact]/{term}).
  /// Use search_stations() for terms that contain a slash.
  [[nodiscard]] Result<std::vector<Station>> stations_by(StationField field, std::string_view term,
                                                         bool exact = false,
                                                         const StationListQuery& query = {},
                                                         const RequestOptions& options = {}) const;

  /// Stations with the given uuids (/json/stations/byuuid).
  [[nodiscard]] Result<std::vector<Station>> stations_by_uuid(
      const std::vector<std::string>& station_uuids, const RequestOptions& options = {}) const;

  /// The station with the given uuid. Fails with ErrorCode::NotFound if it
  /// does not exist.
  [[nodiscard]] Result<Station> station(std::string_view station_uuid,
                                        const RequestOptions& options = {}) const;

  /// Stations with exactly this stream URL (/json/stations/byurl).
  [[nodiscard]] Result<std::vector<Station>> stations_by_url(
      std::string_view url, const RequestOptions& options = {}) const;

  /// Stations served by the given streaming servers (/json/stations/byserveruuid).
  [[nodiscard]] Result<std::vector<Station>> stations_by_server_uuid(
      const std::vector<std::string>& server_uuids, const StationListQuery& query = {},
      const RequestOptions& options = {}) const;

  /// Advanced station search (/json/stations/search).
  [[nodiscard]] Result<std::vector<Station>> search_stations(
      const StationSearch& search, const RequestOptions& options = {}) const;

  /// Stations with the most clicks (/json/stations/topclick).
  [[nodiscard]] Result<std::vector<Station>> top_clicked(const PageQuery& query = {},
                                                         const RequestOptions& options = {}) const;

  /// Stations with the most votes (/json/stations/topvote).
  [[nodiscard]] Result<std::vector<Station>> top_voted(const PageQuery& query = {},
                                                       const RequestOptions& options = {}) const;

  /// Recently clicked stations (/json/stations/lastclick).
  [[nodiscard]] Result<std::vector<Station>> recently_clicked(
      const PageQuery& query = {}, const RequestOptions& options = {}) const;

  /// Recently changed or added stations (/json/stations/lastchange).
  [[nodiscard]] Result<std::vector<Station>> recently_changed(
      const PageQuery& query = {}, const RequestOptions& options = {}) const;

  /// Stations that failed their checks (/json/stations/broken).
  /// PageQuery::hide_broken is ignored.
  [[nodiscard]] Result<std::vector<Station>> broken_stations(
      const PageQuery& query = {}, const RequestOptions& options = {}) const;

  /// The change history of stations (/json/stations/changed).
  [[nodiscard]] Result<std::vector<StationChange>> station_changes(
      const ChangesQuery& query = {}, const RequestOptions& options = {}) const;

  // --- Checks and clicks ---------------------------------------------------

  /// Availability check results (/json/checks).
  [[nodiscard]] Result<std::vector<StationCheck>> checks(const ChecksQuery& query = {},
                                                         const RequestOptions& options = {}) const;

  /// Steps of the latest checks of the given stations (/json/checksteps).
  [[nodiscard]] Result<std::vector<StationCheckStep>> check_steps(
      const std::vector<std::string>& station_uuids, const RequestOptions& options = {}) const;

  /// Clicks (/json/clicks).
  [[nodiscard]] Result<std::vector<StationClick>> clicks(const ClicksQuery& query = {},
                                                         const RequestOptions& options = {}) const;

  // --- Streaming servers ---------------------------------------------------

  /// Known streaming servers (/json/streamingservers).
  [[nodiscard]] Result<std::vector<StreamingServer>> streaming_servers(
      const PageQuery& query = {}, const RequestOptions& options = {}) const;

  /// Streaming servers with the given uuids (/json/streamingservers/byserveruuid).
  [[nodiscard]] Result<std::vector<StreamingServer>> streaming_servers_by_uuid(
      const std::vector<std::string>& server_uuids, const RequestOptions& options = {}) const;

  /// Streaming servers of the given stations (/json/streamingservers/bystationuuid).
  [[nodiscard]] Result<std::vector<StreamingServer>> streaming_servers_by_station(
      const std::vector<std::string>& station_uuids, const RequestOptions& options = {}) const;

  // --- Actions -------------------------------------------------------------

  /// Counts a click for the station and returns its resolved stream URL
  /// (/json/url/{uuid}). Call it every time the user starts playback; this is
  /// what makes stations popular in the database.
  [[nodiscard]] Result<ClickResult> count_click(std::string_view station_uuid,
                                                const RequestOptions& options = {}) const;

  /// Votes for the station (/json/vote/{uuid}).
  [[nodiscard]] Result<VoteResult> vote(std::string_view station_uuid,
                                        const RequestOptions& options = {}) const;

  // --- Escape hatch --------------------------------------------------------

  /// Performs a GET request against any API path (e.g. "/json/stats") with the
  /// usual discovery, failover and limits, and returns the raw body.
  [[nodiscard]] Result<std::string> get_raw(std::string_view path_and_query,
                                            const RequestOptions& options = {}) const;

 private:
  class Impl;
  explicit Client(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

}  // namespace radiobrowser

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
