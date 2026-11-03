// SPDX-License-Identifier: MIT

#include <radiobrowser/curl_transport.hpp>

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// libcurl's API is variadic (curl_easy_setopt/curl_easy_getinfo).
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg,hicpp-vararg)

namespace radiobrowser {

namespace {

struct EasyHandleDeleter {
  void operator()(CURL* handle) const noexcept { curl_easy_cleanup(handle); }
};

struct HeaderListDeleter {
  void operator()(curl_slist* list) const noexcept { curl_slist_free_all(list); }
};

using EasyHandle = std::unique_ptr<CURL, EasyHandleDeleter>;
using HeaderList = std::unique_ptr<curl_slist, HeaderListDeleter>;

// State shared with the libcurl callbacks of one transfer.
struct Transfer {
  std::string body;
  std::size_t max_bytes = 0;
  bool too_large = false;
  const CancellationToken* cancellation = nullptr;
  bool cancelled = false;
};

// curl_global_init is not guaranteed to be thread-safe on older libcurl
// versions; run it exactly once. curl_global_cleanup is deliberately never
// called because other code in the process may still use libcurl.
[[nodiscard]] CURLcode ensure_global_init() {
  static std::once_flag once;
  static CURLcode result = CURLE_OK;
  std::call_once(once, [] { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
  return result;
}

std::size_t write_callback(char* data, std::size_t size, std::size_t count, void* user_data) {
  auto* transfer = static_cast<Transfer*>(user_data);
  const std::size_t bytes = size * count;
  if (transfer->max_bytes != 0 && transfer->body.size() + bytes > transfer->max_bytes) {
    transfer->too_large = true;
    return 0;  // Makes libcurl abort with CURLE_WRITE_ERROR.
  }
  try {
    transfer->body.append(data, bytes);
  } catch (...) {
    return 0;
  }
  return bytes;
}

int progress_callback(void* user_data, curl_off_t /*download_total*/, curl_off_t /*download_now*/,
                      curl_off_t /*upload_total*/, curl_off_t /*upload_now*/) {
  auto* transfer = static_cast<Transfer*>(user_data);
  if (transfer->cancellation != nullptr && transfer->cancellation->is_cancelled()) {
    transfer->cancelled = true;
    return 1;  // Makes libcurl abort with CURLE_ABORTED_BY_CALLBACK.
  }
  return 0;
}

// libcurl takes timeouts as long. 0 means "no timeout", so it is never passed;
// the upper bound keeps the value within a 32-bit long.
[[nodiscard]] long to_curl_millis(std::chrono::milliseconds value) {  // NOLINT(google-runtime-int)
  constexpr std::chrono::milliseconds::rep kMaxMillis = 24LL * 60 * 60 * 1000;
  const auto clamped = std::clamp<std::chrono::milliseconds::rep>(value.count(), 1, kMaxMillis);
  return static_cast<long>(clamped);  // NOLINT(google-runtime-int)
}

[[nodiscard]] bool has_line_break(const std::string& text) {
  return text.find_first_of("\r\n") != std::string::npos;
}

[[nodiscard]] bool is_https_url(std::string_view url) {
  constexpr std::string_view kScheme = "https://";
  if (url.size() < kScheme.size()) {
    return false;
  }
  for (std::size_t i = 0; i < kScheme.size(); ++i) {
    char c = url[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c != kScheme[i]) {
      return false;
    }
  }
  return true;
}

void restrict_protocols(CURL* curl, bool https_only) {
#if LIBCURL_VERSION_NUM >= 0x075500  // 7.85.0
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, https_only ? "https" : "http,https");
#else
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
                   https_only ? static_cast<long>(CURLPROTO_HTTPS)
                              : static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
}

}  // namespace

// ============================================================================
// CurlTransport::Impl
// ============================================================================

class CurlTransport::Impl {
 public:
  explicit Impl(CurlTransportOptions options) : options_(std::move(options)) {}

  [[nodiscard]] const CurlTransportOptions& options() const noexcept { return options_; }

  // Returns an idle handle or creates a new one. Returns null on failure.
  [[nodiscard]] EasyHandle acquire() {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      if (!idle_.empty()) {
        EasyHandle handle = std::move(idle_.back());
        idle_.pop_back();
        return handle;
      }
    }
    return EasyHandle(curl_easy_init());
  }

  // Hands out a pooled handle and returns it to the pool on every exit path.
  class Lease {
   public:
    explicit Lease(Impl& pool) : pool_(pool), handle_(pool.acquire()) {}
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease(Lease&&) = delete;
    Lease& operator=(Lease&&) = delete;
    ~Lease() {
      try {
        pool_.release(std::move(handle_));
      } catch (...) {  // NOLINT(bugprone-empty-catch): the handle is freed by its deleter
      }
    }

    [[nodiscard]] CURL* get() const noexcept { return handle_.get(); }

   private:
    Impl& pool_;
    EasyHandle handle_;
  };

 private:
  // Resets the options (connections and caches survive) and keeps the handle
  // for reuse, or destroys it if the pool is full.
  void release(EasyHandle handle) {
    if (!handle) {
      return;
    }
    // Drops all pointers to the finished transfer's buffers.
    curl_easy_reset(handle.get());
    const std::lock_guard<std::mutex> lock(mutex_);
    if (idle_.size() < options_.max_idle_handles) {
      idle_.push_back(std::move(handle));
    }
  }

  CurlTransportOptions options_;
  std::mutex mutex_;
  std::vector<EasyHandle> idle_;
};

// ============================================================================
// CurlTransport
// ============================================================================

CurlTransport::CurlTransport(CurlTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

CurlTransport::~CurlTransport() = default;

Result<HttpResponse> CurlTransport::perform(const HttpRequest& request) {
  if (request.cancellation.is_cancelled()) {
    return Error{ErrorCode::Cancelled, "the request was cancelled", 0};
  }
  if (request.timeout <= std::chrono::milliseconds::zero()) {
    return Error{ErrorCode::Timeout, "no time left for the request", 0};
  }
  if (const CURLcode init = ensure_global_init(); init != CURLE_OK) {
    return Error{ErrorCode::Internal,
                 std::string("curl_global_init failed: ") + curl_easy_strerror(init), 0};
  }

  HeaderList headers;
  for (const auto& header : request.headers) {
    if (has_line_break(header.name) || has_line_break(header.value)) {
      return Error{ErrorCode::InvalidArgument, "HTTP header contains a line break", 0};
    }
    const std::string line = header.name + ": " + header.value;
    curl_slist* const appended = curl_slist_append(headers.get(), line.c_str());
    if (appended == nullptr) {
      return Error{ErrorCode::Internal, "curl_slist_append failed", 0};
    }
    if (!headers) {
      headers.reset(appended);  // The first append creates the list.
    }
  }

  // Declared before the lease: the handle refers to these until it is reset.
  Transfer transfer;
  transfer.max_bytes = request.max_response_bytes;
  transfer.cancellation = &request.cancellation;
  std::array<char, CURL_ERROR_SIZE> error_buffer{};

  const Impl::Lease lease(*impl_);
  CURL* const curl = lease.get();
  if (curl == nullptr) {
    return Error{ErrorCode::Internal, "curl_easy_init failed", 0};
  }
  const CurlTransportOptions& options = impl_->options();

  curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, to_curl_millis(request.timeout));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                   to_curl_millis(std::min(request.connect_timeout, request.timeout)));
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, options.follow_redirects ? 1L : 0L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, options.max_redirects);
  restrict_protocols(curl, is_https_url(request.url));
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  if (!options.ca_bundle_path.empty()) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, options.ca_bundle_path.c_str());
  }
  if (!options.proxy.empty()) {
    curl_easy_setopt(curl, CURLOPT_PROXY, options.proxy.c_str());
  }
  // Accept every encoding libcurl was built with (gzip, br, ...).
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &progress_callback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

  if (request.method == HttpMethod::Post) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(request.body.size()));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
  } else {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  }

  const CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    const std::string message =
        error_buffer[0] != '\0' ? std::string(error_buffer.data()) : curl_easy_strerror(code);
    if (transfer.too_large) {
      return Error{ErrorCode::ResponseTooLarge,
                   "response exceeds " + std::to_string(request.max_response_bytes) + " bytes", 0};
    }
    if (transfer.cancelled || request.cancellation.is_cancelled()) {
      return Error{ErrorCode::Cancelled, "the request was cancelled", 0};
    }
    if (code == CURLE_OPERATION_TIMEDOUT) {
      return Error{ErrorCode::Timeout, message, 0};
    }
    return Error{ErrorCode::Network, message, 0};
  }

  long status = 0;  // NOLINT(google-runtime-int): libcurl API type
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  return HttpResponse{static_cast<int>(status), std::move(transfer.body)};
}

}  // namespace radiobrowser

// NOLINTEND(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
