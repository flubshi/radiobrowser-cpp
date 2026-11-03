// SPDX-License-Identifier: MIT

#include <radiobrowser/client.hpp>
#include <radiobrowser/config.hpp>

#if RADIOBROWSER_HAS_CURL
#include <radiobrowser/curl_transport.hpp>
#endif

#include "detail/call_helpers.hpp"
#include "detail/hostname.hpp"
#include "detail/json_mapping.hpp"
#include "detail/url.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace radiobrowser {

namespace {

using detail::SteadyClock;

constexpr std::chrono::milliseconds kLockPollInterval{50};

[[nodiscard]] constexpr std::string_view to_api_value(StationOrder order) noexcept {
  switch (order) {
    case StationOrder::Name:
      return "name";
    case StationOrder::Url:
      return "url";
    case StationOrder::Homepage:
      return "homepage";
    case StationOrder::Favicon:
      return "favicon";
    case StationOrder::Tags:
      return "tags";
    case StationOrder::Country:
      return "country";
    case StationOrder::State:
      return "state";
    case StationOrder::Language:
      return "language";
    case StationOrder::Votes:
      return "votes";
    case StationOrder::Codec:
      return "codec";
    case StationOrder::Bitrate:
      return "bitrate";
    case StationOrder::LastCheckOk:
      return "lastcheckok";
    case StationOrder::LastCheckTime:
      return "lastchecktime";
    case StationOrder::ClickTimestamp:
      return "clicktimestamp";
    case StationOrder::ClickCount:
      return "clickcount";
    case StationOrder::ClickTrend:
      return "clicktrend";
    case StationOrder::ChangeTimestamp:
      return "changetimestamp";
    case StationOrder::Random:
      return "random";
  }
  return "name";
}

[[nodiscard]] constexpr std::string_view to_api_value(ListOrder order) noexcept {
  return order == ListOrder::StationCount ? "stationcount" : "name";
}

[[nodiscard]] constexpr std::string_view to_api_value(StationField field) noexcept {
  switch (field) {
    case StationField::Name:
      return "name";
    case StationField::Codec:
      return "codec";
    case StationField::Country:
      return "country";
    case StationField::CountryCode:
      return "countrycode";
    case StationField::State:
      return "state";
    case StationField::Language:
      return "language";
    case StationField::Tag:
      return "tag";
  }
  return "name";
}

// Statuses that indicate an overloaded or broken server rather than a bad request.
[[nodiscard]] constexpr bool is_retryable_status(int status) noexcept {
  return status == 408 || status == 429 || status >= 500;
}

[[nodiscard]] bool is_valid_header_value(std::string_view value) noexcept {
  return std::none_of(value.begin(), value.end(), [](char c) {
    const auto byte = static_cast<unsigned char>(c);
    return (byte < 0x20 && c != '\t') || byte == 0x7F;
  });
}

[[nodiscard]] Error invalid_argument(std::string message) {
  return Error{ErrorCode::InvalidArgument, std::move(message), 0};
}

[[nodiscard]] Error moved_from_error() {
  return Error{ErrorCode::Internal, "the client has been moved from", 0};
}

[[nodiscard]] std::uint64_t to_u64(std::size_t value) noexcept {
  return static_cast<std::uint64_t>(value);
}

[[nodiscard]] std::mt19937 make_random_engine() {
  try {
    std::random_device device;
    return std::mt19937(device());
  } catch (const std::exception&) {
    const auto seed = SteadyClock::now().time_since_epoch().count();
    return std::mt19937(static_cast<std::mt19937::result_type>(seed));
  }
}

}  // namespace

// ============================================================================
// Client::Impl
// ============================================================================

class Client::Impl {
 public:
  Impl(ClientOptions options, std::shared_ptr<HttpTransport> transport,
       std::shared_ptr<ServerDiscovery> discovery)
      : options_(std::move(options)),
        transport_(std::move(transport)),
        discovery_(std::move(discovery)),
        random_(make_random_engine()) {}

  [[nodiscard]] const ClientOptions& client_options() const noexcept { return options_; }

  [[nodiscard]] std::size_t limit_or_default(const std::optional<std::size_t>& limit) const {
    return limit.value_or(options_.default_limit);
  }

  [[nodiscard]] bool hide_broken_or_default(const std::optional<bool>& hide_broken) const {
    return hide_broken.value_or(options_.hide_broken);
  }

  // Runs one API call: GET with failover, then parse. Never throws.
  template <typename T, typename Parser>
  [[nodiscard]] Result<T> fetch(std::string_view path_and_query, const RequestOptions& request,
                                Parser parse) {
    try {
      auto body = get(path_and_query, request);
      if (!body) {
        return std::move(body).error();
      }
      Result<T> parsed = parse(*body);
      if (!parsed) {
        log(LogLevel::Error,
            "GET " + std::string(path_and_query) + ": " + to_string(parsed.error()));
      }
      return parsed;
    } catch (const std::exception& e) {
      return Error{ErrorCode::Internal, std::string("unexpected exception: ") + e.what(), 0};
    } catch (...) {
      return Error{ErrorCode::Internal, "unexpected unknown exception", 0};
    }
  }

  [[nodiscard]] Result<std::string> get_safely(std::string_view path_and_query,
                                               const RequestOptions& request) {
    try {
      return get(path_and_query, request);
    } catch (const std::exception& e) {
      return Error{ErrorCode::Internal, std::string("unexpected exception: ") + e.what(), 0};
    } catch (...) {
      return Error{ErrorCode::Internal, "unexpected unknown exception", 0};
    }
  }

  [[nodiscard]] Result<std::vector<std::string>> servers(const RequestOptions& request,
                                                         bool force_refresh) {
    try {
      return ensure_servers(make_context(request), force_refresh);
    } catch (const std::exception& e) {
      return Error{ErrorCode::Internal, std::string("unexpected exception: ") + e.what(), 0};
    } catch (...) {
      return Error{ErrorCode::Internal, "unexpected unknown exception", 0};
    }
  }

 private:
  struct CallContext {
    SteadyClock::time_point deadline;
    CancellationToken cancellation;
  };

  [[nodiscard]] CallContext make_context(const RequestOptions& request) const {
    return CallContext{SteadyClock::now() + request.timeout.value_or(options_.timeout),
                       request.cancellation};
  }

  void log(LogLevel level, const std::string& message) const noexcept {
    if (!options_.logger) {
      return;
    }
    try {
      options_.logger(level, message);
    } catch (...) {  // NOLINT(bugprone-empty-catch): a faulty logger must not break calls
    }
  }

  // Performs a GET request with failover across the known servers.
  [[nodiscard]] Result<std::string> get(std::string_view path_and_query,
                                        const RequestOptions& request) {
    const CallContext context = make_context(request);
    if (context.cancellation.is_cancelled()) {
      return Error{ErrorCode::Cancelled, "the call was cancelled", 0};
    }
    auto server_list = ensure_servers(context, false);
    if (!server_list) {
      log(LogLevel::Error, "server discovery failed: " + to_string(server_list.error()));
      return std::move(server_list).error();
    }

    const std::size_t count = server_list->size();
    const std::size_t start = preferred_index() % count;
    std::vector<std::string> failures;
    for (std::size_t attempt = 0; attempt < count; ++attempt) {
      const std::string& base_url = (*server_list)[(start + attempt) % count];
      if (context.cancellation.is_cancelled()) {
        return Error{ErrorCode::Cancelled, "the call was cancelled", 0};
      }
      const auto remaining = detail::remaining_until(context.deadline);
      if (remaining <= std::chrono::milliseconds::zero()) {
        return timeout_error(path_and_query, failures);
      }

      HttpRequest http;
      http.url = base_url + std::string(path_and_query);
      http.headers = {{"User-Agent", options_.user_agent}, {"Accept", "application/json"}};
      http.timeout = std::min(remaining, options_.attempt_timeout);
      http.connect_timeout = std::min(http.timeout, options_.connect_timeout);
      http.max_response_bytes = options_.max_response_bytes;
      http.cancellation = context.cancellation;

      log(LogLevel::Debug, "GET " + http.url);
      auto response = detail::perform_safely(*transport_, http);
      if (response) {
        if (detail::is_success_status(response->status)) {
          remember_server(base_url);
          return std::move(response->body);
        }
        if (!is_retryable_status(response->status)) {
          // A client error; every other server would answer the same way.
          remember_server(base_url);
          return Error{ErrorCode::HttpStatus,
                       "GET " + std::string(path_and_query) + " returned HTTP status " +
                           std::to_string(response->status),
                       response->status};
        }
        failures.push_back(base_url + ": HTTP status " + std::to_string(response->status));
      } else {
        const Error& error = response.error();
        switch (error.code) {
          case ErrorCode::Timeout:
            if (SteadyClock::now() >= context.deadline) {
              failures.push_back(base_url + ": " + to_string(error));
              return timeout_error(path_and_query, failures);
            }
            break;  // Only this attempt timed out; try the next server.
          case ErrorCode::Network:
          case ErrorCode::HttpStatus:
          case ErrorCode::InvalidResponse:
          case ErrorCode::NoServers:
          case ErrorCode::AllServersFailed:
          case ErrorCode::NotFound:
            break;
          case ErrorCode::Cancelled:
          case ErrorCode::ResponseTooLarge:
          case ErrorCode::InvalidArgument:
          case ErrorCode::Internal:
            // Failing over would not help.
            return std::move(response).error();
        }
        failures.push_back(base_url + ": " + to_string(error));
      }
      log(LogLevel::Warning, "request failed, trying next server: " + failures.back());
      skip_server(base_url);
    }

    // Every server failed: the list is probably stale, rediscover next time.
    invalidate_servers();
    Error error{ErrorCode::AllServersFailed,
                "GET " + std::string(path_and_query) + ": " + detail::join_messages(failures), 0};
    log(LogLevel::Error, to_string(error));
    return error;
  }

  [[nodiscard]] static Error timeout_error(std::string_view path_and_query,
                                           const std::vector<std::string>& failures) {
    std::string message = "GET " + std::string(path_and_query) + ": deadline expired";
    if (!failures.empty()) {
      message += " (" + detail::join_messages(failures) + ")";
    }
    return Error{ErrorCode::Timeout, std::move(message), 0};
  }

  // Returns the cached server list or runs discovery. Only one discovery runs
  // at a time; concurrent callers wait for it and reuse its result.
  [[nodiscard]] Result<std::vector<std::string>> ensure_servers(const CallContext& context,
                                                                bool force_refresh) {
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (!force_refresh && has_fresh_servers()) {
      return servers_;
    }
    const std::uint64_t generation_seen = generation_;
    while (discovery_running_) {
      if (context.cancellation.is_cancelled()) {
        return Error{ErrorCode::Cancelled, "the call was cancelled", 0};
      }
      if (SteadyClock::now() >= context.deadline) {
        return Error{ErrorCode::Timeout, "deadline expired while waiting for server discovery", 0};
      }
      discovery_finished_.wait_for(lock, kLockPollInterval);
    }
    // Another caller may have completed a discovery while we were waiting.
    if (generation_ != generation_seen && has_fresh_servers()) {
      return servers_;
    }

    discovery_running_ = true;
    const DiscoveryGuard guard(*this, lock);
    lock.unlock();

    DiscoveryRequest request;
    request.deadline = context.deadline;
    request.cancellation = context.cancellation;
    request.transport = transport_.get();
    request.user_agent = options_.user_agent;
    request.max_response_bytes = options_.max_response_bytes;
    auto discovered = detail::discover_safely(*discovery_, request);
    if (!discovered) {
      return discovered;
    }

    std::vector<std::string> discovered_servers;
    for (const auto& url : *discovered) {
      if (auto base_url = detail::normalize_base_url(url)) {
        if (std::find(discovered_servers.begin(), discovered_servers.end(), *base_url) ==
            discovered_servers.end()) {
          discovered_servers.push_back(std::move(*base_url));
        }
      }
    }
    if (discovered_servers.empty()) {
      return Error{ErrorCode::NoServers, "server discovery returned no valid base URL", 0};
    }

    lock.lock();
    if (options_.shuffle_servers) {
      std::shuffle(discovered_servers.begin(), discovered_servers.end(), random_);
    }
    servers_ = discovered_servers;
    servers_fetched_at_ = SteadyClock::now();
    servers_valid_ = true;
    preferred_index_ = 0;
    ++generation_;
    lock.unlock();

    log(LogLevel::Info, "using " + std::to_string(discovered_servers.size()) +
                            " server(s): " + detail::join_messages(discovered_servers));
    return discovered_servers;
  }

  // Marks the running discovery as finished on every exit path, including
  // exceptions, and wakes up the callers waiting for it.
  class DiscoveryGuard {
   public:
    DiscoveryGuard(Impl& impl, std::unique_lock<std::mutex>& lock) noexcept
        : impl_(impl), lock_(lock) {}
    DiscoveryGuard(const DiscoveryGuard&) = delete;
    DiscoveryGuard& operator=(const DiscoveryGuard&) = delete;
    DiscoveryGuard(DiscoveryGuard&&) = delete;
    DiscoveryGuard& operator=(DiscoveryGuard&&) = delete;
    ~DiscoveryGuard() {
      if (!lock_.owns_lock()) {
        lock_.lock();
      }
      impl_.discovery_running_ = false;
      impl_.discovery_finished_.notify_all();
    }

   private:
    Impl& impl_;
    std::unique_lock<std::mutex>& lock_;
  };

  // Requires state_mutex_.
  [[nodiscard]] bool has_fresh_servers() const {
    return servers_valid_ && !servers_.empty() &&
           SteadyClock::now() - servers_fetched_at_ < options_.server_list_ttl;
  }

  [[nodiscard]] std::size_t preferred_index() const {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    return preferred_index_;
  }

  // Makes the server that just answered the first choice for later calls.
  void remember_server(const std::string& base_url) {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    const auto it = std::find(servers_.begin(), servers_.end(), base_url);
    if (it != servers_.end()) {
      preferred_index_ = static_cast<std::size_t>(std::distance(servers_.begin(), it));
    }
  }

  // Moves the preference past a server that just failed.
  void skip_server(const std::string& base_url) {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    if (!servers_.empty() && preferred_index_ < servers_.size() &&
        servers_[preferred_index_] == base_url) {
      preferred_index_ = (preferred_index_ + 1) % servers_.size();
    }
  }

  void invalidate_servers() {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    servers_valid_ = false;
  }

  ClientOptions options_;
  std::shared_ptr<HttpTransport> transport_;
  std::shared_ptr<ServerDiscovery> discovery_;

  mutable std::mutex state_mutex_;
  std::vector<std::string> servers_;
  SteadyClock::time_point servers_fetched_at_;
  bool servers_valid_ = false;
  std::size_t preferred_index_ = 0;
  std::uint64_t generation_ = 0;
  std::mt19937 random_;
  bool discovery_running_ = false;
  std::condition_variable discovery_finished_;
};

// ============================================================================
// Query helpers
// ============================================================================

namespace {

void add_paging(detail::QueryBuilder& builder, std::size_t offset,
                std::optional<std::size_t> limit) {
  if (offset > 0) {
    builder.add_number("offset", to_u64(offset));
  }
  if (limit) {
    builder.add_number("limit", to_u64(*limit));
  }
}

[[nodiscard]] std::string list_path(std::string_view endpoint, const std::string& filter) {
  std::string path = "/json/" + std::string(endpoint);
  if (!filter.empty()) {
    path += "/" + detail::percent_encode(filter);
  }
  return path;
}

}  // namespace

// ============================================================================
// Client
// ============================================================================

Result<Client> Client::create(ClientOptions options) {
  try {
    if (options.user_agent.empty()) {
      options.user_agent = std::string("radiobrowser-cpp/") + kVersion;
    }
    if (!is_valid_header_value(options.user_agent)) {
      return invalid_argument("user_agent must not contain control characters");
    }
    if (options.timeout <= std::chrono::milliseconds::zero() ||
        options.attempt_timeout <= std::chrono::milliseconds::zero() ||
        options.connect_timeout <= std::chrono::milliseconds::zero()) {
      return invalid_argument("timeouts must be positive");
    }
    if (options.default_limit == 0) {
      return invalid_argument("default_limit must be positive");
    }

    std::shared_ptr<HttpTransport> transport = options.transport;
    if (!transport) {
#if RADIOBROWSER_HAS_CURL
      transport = std::make_shared<CurlTransport>();
#else
      return invalid_argument(
          "no HTTP transport configured and the library was built without libcurl");
#endif
    }
    std::shared_ptr<ServerDiscovery> discovery =
        options.discovery ? options.discovery : make_default_discovery();

    return Client(
        std::make_unique<Impl>(std::move(options), std::move(transport), std::move(discovery)));
  } catch (const std::exception& e) {
    return Error{ErrorCode::Internal, std::string("failed to create client: ") + e.what(), 0};
  }
}

Client::Client(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Client::Client(Client&& other) noexcept = default;
Client& Client::operator=(Client&& other) noexcept = default;
Client::~Client() = default;

const std::string& Client::user_agent() const noexcept {
  static const std::string empty_user_agent;
  return impl_ ? impl_->client_options().user_agent : empty_user_agent;
}

// --- Servers -----------------------------------------------------------------

Result<std::vector<std::string>> Client::servers(const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  return impl_->servers(options, false);
}

Result<std::vector<std::string>> Client::refresh_servers(const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  return impl_->servers(options, true);
}

Result<std::vector<ServerMirror>> Client::server_mirrors(const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  return impl_->fetch<std::vector<ServerMirror>>("/json/servers", options,
                                                 detail::parse_server_mirrors);
}

Result<ServerStats> Client::stats(const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  return impl_->fetch<ServerStats>("/json/stats", options, detail::parse_server_stats);
}

Result<ServerConfig> Client::config(const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  return impl_->fetch<ServerConfig>("/json/config", options, detail::parse_server_config);
}

// --- Lists -------------------------------------------------------------------

namespace {

[[nodiscard]] std::string list_query(const ListQuery& query, bool hide_broken) {
  detail::QueryBuilder builder;
  builder.add("order", to_api_value(query.order));
  builder.add_bool("reverse", query.reverse);
  builder.add_bool("hidebroken", hide_broken);
  add_paging(builder, query.offset, query.limit);
  return builder.str();
}

}  // namespace

Result<std::vector<Country>> Client::countries(const ListQuery& query,
                                               const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = list_path("countries", query.filter) +
                           list_query(query, impl_->hide_broken_or_default(query.hide_broken));
  return impl_->fetch<std::vector<Country>>(path, options, detail::parse_countries);
}

Result<std::vector<CountryCodeCount>> Client::country_codes(const ListQuery& query,
                                                            const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = list_path("countrycodes", query.filter) +
                           list_query(query, impl_->hide_broken_or_default(query.hide_broken));
  return impl_->fetch<std::vector<CountryCodeCount>>(path, options, detail::parse_named_counts);
}

Result<std::vector<CodecCount>> Client::codecs(const ListQuery& query,
                                               const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = list_path("codecs", query.filter) +
                           list_query(query, impl_->hide_broken_or_default(query.hide_broken));
  return impl_->fetch<std::vector<CodecCount>>(path, options, detail::parse_named_counts);
}

Result<std::vector<State>> Client::states(const StateQuery& query,
                                          const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  std::string path = "/json/states";
  if (!query.country.empty()) {
    path +=
        "/" + detail::percent_encode(query.country) + "/" + detail::percent_encode(query.filter);
  } else if (!query.filter.empty()) {
    path += "/" + detail::percent_encode(query.filter);
  }
  const ListQuery list{query.filter,      query.order,  query.reverse,
                       query.hide_broken, query.offset, query.limit};
  path += list_query(list, impl_->hide_broken_or_default(query.hide_broken));
  return impl_->fetch<std::vector<State>>(path, options, detail::parse_states);
}

Result<std::vector<Language>> Client::languages(const ListQuery& query,
                                                const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = list_path("languages", query.filter) +
                           list_query(query, impl_->hide_broken_or_default(query.hide_broken));
  return impl_->fetch<std::vector<Language>>(path, options, detail::parse_languages);
}

Result<std::vector<TagCount>> Client::tags(const ListQuery& query,
                                           const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = list_path("tags", query.filter) +
                           list_query(query, impl_->hide_broken_or_default(query.hide_broken));
  return impl_->fetch<std::vector<TagCount>>(path, options, detail::parse_named_counts);
}

// --- Stations ----------------------------------------------------------------

namespace {

[[nodiscard]] std::string station_list_query(const StationListQuery& query, bool hide_broken,
                                             std::size_t limit) {
  detail::QueryBuilder builder;
  builder.add("order", to_api_value(query.order));
  builder.add_bool("reverse", query.reverse);
  builder.add_bool("hidebroken", hide_broken);
  add_paging(builder, query.offset, limit);
  return builder.str();
}

[[nodiscard]] std::string page_query(const PageQuery& query, std::optional<bool> hide_broken,
                                     std::size_t limit) {
  detail::QueryBuilder builder;
  if (hide_broken) {
    builder.add_bool("hidebroken", *hide_broken);
  }
  add_paging(builder, query.offset, limit);
  return builder.str();
}

}  // namespace

Result<std::vector<Station>> Client::stations(const StationListQuery& query,
                                              const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path =
      "/json/stations" + station_list_query(query, impl_->hide_broken_or_default(query.hide_broken),
                                            impl_->limit_or_default(query.limit));
  return impl_->fetch<std::vector<Station>>(path, options, detail::parse_stations);
}

Result<std::vector<Station>> Client::stations_by(StationField field, std::string_view term,
                                                 bool exact, const StationListQuery& query,
                                                 const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  if (term.empty()) {
    return invalid_argument("the search term must not be empty");
  }
  const bool use_exact = exact || field == StationField::CountryCode;
  const std::string path =
      "/json/stations/by" + std::string(to_api_value(field)) + (use_exact ? "exact" : "") + "/" +
      detail::percent_encode(term) +
      station_list_query(query, impl_->hide_broken_or_default(query.hide_broken),
                         impl_->limit_or_default(query.limit));
  return impl_->fetch<std::vector<Station>>(path, options, detail::parse_stations);
}

Result<std::vector<Station>> Client::stations_by_uuid(const std::vector<std::string>& station_uuids,
                                                      const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string uuids = detail::join_comma(station_uuids);
  if (uuids.empty()) {
    return std::vector<Station>{};
  }
  detail::QueryBuilder builder;
  builder.add("uuids", uuids);
  return impl_->fetch<std::vector<Station>>("/json/stations/byuuid" + builder.str(), options,
                                            detail::parse_stations);
}

Result<Station> Client::station(std::string_view station_uuid,
                                const RequestOptions& options) const {
  if (station_uuid.empty()) {
    return invalid_argument("the station uuid must not be empty");
  }
  auto matches = stations_by_uuid({std::string(station_uuid)}, options);
  if (!matches) {
    return std::move(matches).error();
  }
  if (matches->empty()) {
    return Error{ErrorCode::NotFound, "no station with uuid " + std::string(station_uuid), 0};
  }
  return std::move(matches->front());
}

Result<std::vector<Station>> Client::stations_by_url(std::string_view url,
                                                     const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  if (url.empty()) {
    return invalid_argument("the URL must not be empty");
  }
  detail::QueryBuilder builder;
  builder.add("url", url);
  return impl_->fetch<std::vector<Station>>("/json/stations/byurl" + builder.str(), options,
                                            detail::parse_stations);
}

Result<std::vector<Station>> Client::stations_by_server_uuid(
    const std::vector<std::string>& server_uuids, const StationListQuery& query,
    const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string uuids = detail::join_comma(server_uuids);
  if (uuids.empty()) {
    return std::vector<Station>{};
  }
  std::string path = "/json/stations/byserveruuid" +
                     station_list_query(query, impl_->hide_broken_or_default(query.hide_broken),
                                        impl_->limit_or_default(query.limit));
  path += "&uuids=" + detail::percent_encode(uuids);
  return impl_->fetch<std::vector<Station>>(path, options, detail::parse_stations);
}

Result<std::vector<Station>> Client::search_stations(const StationSearch& search,
                                                     const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  detail::QueryBuilder builder;
  builder.add_if_not_empty("name", search.name);
  if (search.name_exact) {
    builder.add_bool("nameExact", true);
  }
  builder.add_if_not_empty("country", search.country);
  if (search.country_exact) {
    builder.add_bool("countryExact", true);
  }
  builder.add_if_not_empty("countrycode", search.country_code);
  builder.add_if_not_empty("state", search.state);
  if (search.state_exact) {
    builder.add_bool("stateExact", true);
  }
  builder.add_if_not_empty("language", search.language);
  if (search.language_exact) {
    builder.add_bool("languageExact", true);
  }
  builder.add_if_not_empty("tag", search.tag);
  if (search.tag_exact) {
    builder.add_bool("tagExact", true);
  }
  builder.add_if_not_empty("tagList", detail::join_comma(search.tag_list));
  builder.add_if_not_empty("codec", search.codec);
  if (search.bitrate_min) {
    builder.add_number("bitrateMin", *search.bitrate_min);
  }
  if (search.bitrate_max) {
    builder.add_number("bitrateMax", *search.bitrate_max);
  }
  if (search.has_geo_info) {
    builder.add_bool("has_geo_info", *search.has_geo_info);
  }
  if (search.has_extended_info) {
    builder.add_bool("has_extended_info", *search.has_extended_info);
  }
  if (search.is_https) {
    builder.add_bool("is_https", *search.is_https);
  }
  builder.add("order", to_api_value(search.order));
  builder.add_bool("reverse", search.reverse);
  builder.add_bool("hidebroken", impl_->hide_broken_or_default(search.hide_broken));
  add_paging(builder, search.offset, impl_->limit_or_default(search.limit));
  return impl_->fetch<std::vector<Station>>("/json/stations/search" + builder.str(), options,
                                            detail::parse_stations);
}

Result<std::vector<Station>> Client::top_clicked(const PageQuery& query,
                                                 const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = "/json/stations/topclick" +
                           page_query(query, impl_->hide_broken_or_default(query.hide_broken),
                                      impl_->limit_or_default(query.limit));
  return impl_->fetch<std::vector<Station>>(path, options, detail::parse_stations);
}

Result<std::vector<Station>> Client::top_voted(const PageQuery& query,
                                               const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path =
      "/json/stations/topvote" + page_query(query, impl_->hide_broken_or_default(query.hide_broken),
                                            impl_->limit_or_default(query.limit));
  return impl_->fetch<std::vector<Station>>(path, options, detail::parse_stations);
}

Result<std::vector<Station>> Client::recently_clicked(const PageQuery& query,
                                                      const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = "/json/stations/lastclick" +
                           page_query(query, impl_->hide_broken_or_default(query.hide_broken),
                                      impl_->limit_or_default(query.limit));
  return impl_->fetch<std::vector<Station>>(path, options, detail::parse_stations);
}

Result<std::vector<Station>> Client::recently_changed(const PageQuery& query,
                                                      const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = "/json/stations/lastchange" +
                           page_query(query, impl_->hide_broken_or_default(query.hide_broken),
                                      impl_->limit_or_default(query.limit));
  return impl_->fetch<std::vector<Station>>(path, options, detail::parse_stations);
}

Result<std::vector<Station>> Client::broken_stations(const PageQuery& query,
                                                     const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = "/json/stations/broken" +
                           page_query(query, std::nullopt, impl_->limit_or_default(query.limit));
  return impl_->fetch<std::vector<Station>>(path, options, detail::parse_stations);
}

Result<std::vector<StationChange>> Client::station_changes(const ChangesQuery& query,
                                                           const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  std::string path = "/json/stations/changed";
  if (!query.station_uuid.empty()) {
    path += "/" + detail::percent_encode(query.station_uuid);
  }
  detail::QueryBuilder builder;
  builder.add_if_not_empty("lastchangeuuid", query.last_change_uuid);
  builder.add_number("limit", to_u64(impl_->limit_or_default(query.limit)));
  return impl_->fetch<std::vector<StationChange>>(path + builder.str(), options,
                                                  detail::parse_station_changes);
}

// --- Checks and clicks -------------------------------------------------------

Result<std::vector<StationCheck>> Client::checks(const ChecksQuery& query,
                                                 const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  std::string path = "/json/checks";
  if (!query.station_uuid.empty()) {
    path += "/" + detail::percent_encode(query.station_uuid);
  }
  detail::QueryBuilder builder;
  builder.add_if_not_empty("lastcheckuuid", query.last_check_uuid);
  if (query.seconds) {
    builder.add_number("seconds", static_cast<std::uint64_t>(std::max<std::int64_t>(
                                      0, static_cast<std::int64_t>(query.seconds->count()))));
  }
  builder.add_number("limit", to_u64(impl_->limit_or_default(query.limit)));
  return impl_->fetch<std::vector<StationCheck>>(path + builder.str(), options,
                                                 detail::parse_station_checks);
}

Result<std::vector<StationCheckStep>> Client::check_steps(
    const std::vector<std::string>& station_uuids, const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string uuids = detail::join_comma(station_uuids);
  if (uuids.empty()) {
    return std::vector<StationCheckStep>{};
  }
  detail::QueryBuilder builder;
  builder.add("uuids", uuids);
  return impl_->fetch<std::vector<StationCheckStep>>("/json/checksteps" + builder.str(), options,
                                                     detail::parse_check_steps);
}

Result<std::vector<StationClick>> Client::clicks(const ClicksQuery& query,
                                                 const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  std::string path = "/json/clicks";
  if (!query.station_uuid.empty()) {
    path += "/" + detail::percent_encode(query.station_uuid);
  }
  detail::QueryBuilder builder;
  builder.add_if_not_empty("lastclickuuid", query.last_click_uuid);
  if (query.seconds) {
    builder.add_number("seconds", static_cast<std::uint64_t>(std::max<std::int64_t>(
                                      0, static_cast<std::int64_t>(query.seconds->count()))));
  }
  return impl_->fetch<std::vector<StationClick>>(path + builder.str(), options,
                                                 detail::parse_station_clicks);
}

// --- Streaming servers -------------------------------------------------------

Result<std::vector<StreamingServer>> Client::streaming_servers(
    const PageQuery& query, const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string path = "/json/streamingservers" +
                           page_query(query, std::nullopt, impl_->limit_or_default(query.limit));
  return impl_->fetch<std::vector<StreamingServer>>(path, options, detail::parse_streaming_servers);
}

Result<std::vector<StreamingServer>> Client::streaming_servers_by_uuid(
    const std::vector<std::string>& server_uuids, const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string uuids = detail::join_comma(server_uuids);
  if (uuids.empty()) {
    return std::vector<StreamingServer>{};
  }
  detail::QueryBuilder builder;
  builder.add("uuids", uuids);
  return impl_->fetch<std::vector<StreamingServer>>(
      "/json/streamingservers/byserveruuid" + builder.str(), options,
      detail::parse_streaming_servers);
}

Result<std::vector<StreamingServer>> Client::streaming_servers_by_station(
    const std::vector<std::string>& station_uuids, const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  const std::string uuids = detail::join_comma(station_uuids);
  if (uuids.empty()) {
    return std::vector<StreamingServer>{};
  }
  detail::QueryBuilder builder;
  builder.add("uuids", uuids);
  return impl_->fetch<std::vector<StreamingServer>>(
      "/json/streamingservers/bystationuuid" + builder.str(), options,
      detail::parse_streaming_servers);
}

// --- Actions -----------------------------------------------------------------

Result<ClickResult> Client::count_click(std::string_view station_uuid,
                                        const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  if (station_uuid.empty()) {
    return invalid_argument("the station uuid must not be empty");
  }
  return impl_->fetch<ClickResult>("/json/url/" + detail::percent_encode(station_uuid), options,
                                   detail::parse_click_result);
}

Result<VoteResult> Client::vote(std::string_view station_uuid,
                                const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  if (station_uuid.empty()) {
    return invalid_argument("the station uuid must not be empty");
  }
  return impl_->fetch<VoteResult>("/json/vote/" + detail::percent_encode(station_uuid), options,
                                  detail::parse_vote_result);
}

// --- Escape hatch ------------------------------------------------------------

Result<std::string> Client::get_raw(std::string_view path_and_query,
                                    const RequestOptions& options) const {
  if (!impl_) {
    return moved_from_error();
  }
  if (path_and_query.empty() || path_and_query.front() != '/') {
    return invalid_argument("the path must start with '/'");
  }
  if (!is_valid_header_value(path_and_query) ||
      path_and_query.find(' ') != std::string_view::npos) {
    return invalid_argument("the path must not contain spaces or control characters");
  }
  return impl_->get_safely(path_and_query, options);
}

}  // namespace radiobrowser
