// SPDX-License-Identifier: MIT

#include "detail/system_resolver.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace radiobrowser::detail {

namespace {

#if defined(_WIN32)
// Winsock must be initialised per process; WSAStartup is reference counted.
class WinsockSession {
 public:
  WinsockSession() noexcept {
    WSADATA data{};
    ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  WinsockSession(const WinsockSession&) = delete;
  WinsockSession& operator=(const WinsockSession&) = delete;
  WinsockSession(WinsockSession&&) = delete;
  WinsockSession& operator=(WinsockSession&&) = delete;
  ~WinsockSession() {
    if (ok_) {
      WSACleanup();
    }
  }
  [[nodiscard]] bool ok() const noexcept { return ok_; }

 private:
  bool ok_ = false;
};
#endif

struct AddrInfoDeleter {
  void operator()(addrinfo* info) const noexcept {
    if (info != nullptr) {
      freeaddrinfo(info);
    }
  }
};

[[nodiscard]] std::string describe_gai_error(int code) {
#if defined(_WIN32)
  return "getaddrinfo failed with error " + std::to_string(code);
#else
  return std::string("getaddrinfo failed: ") + gai_strerror(code);
#endif
}

}  // namespace

Result<std::vector<std::string>> resolve_reverse_hostnames(const std::string& host) {
#if defined(_WIN32)
  const WinsockSession winsock;
  if (!winsock.ok()) {
    return Error{ErrorCode::Network, "WSAStartup failed", 0};
  }
#endif

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* raw_result = nullptr;
  const int rc = getaddrinfo(host.c_str(), "443", &hints, &raw_result);
  const std::unique_ptr<addrinfo, AddrInfoDeleter> result(raw_result);
  if (rc != 0) {
    return Error{ErrorCode::Network, describe_gai_error(rc) + " (" + host + ")", 0};
  }

  std::vector<std::string> names;
  for (const addrinfo* entry = result.get(); entry != nullptr; entry = entry->ai_next) {
    if (entry->ai_addr == nullptr) {
      continue;
    }
    std::array<char, NI_MAXHOST> name{};
#if defined(_WIN32)
    const int lookup =
        getnameinfo(entry->ai_addr, static_cast<socklen_t>(entry->ai_addrlen), name.data(),
                    static_cast<DWORD>(name.size()), nullptr, 0, NI_NAMEREQD);
#else
    const int lookup = getnameinfo(entry->ai_addr, entry->ai_addrlen, name.data(),
                                   static_cast<socklen_t>(name.size()), nullptr, 0, NI_NAMEREQD);
#endif
    if (lookup == 0) {
      names.emplace_back(name.data());
    }
  }
  return names;
}

}  // namespace radiobrowser::detail
