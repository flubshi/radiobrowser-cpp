// SPDX-License-Identifier: MIT
// Uses the library like a downstream project would, without network access.

#include <radiobrowser/radiobrowser.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class OfflineTransport : public radiobrowser::HttpTransport {
 public:
  radiobrowser::Result<radiobrowser::HttpResponse> perform(
      const radiobrowser::HttpRequest& request) override {
    if (request.url == "https://mirror.example/json/stats") {
      return radiobrowser::HttpResponse{200, R"({"status":"OK","stations":1})"};
    }
    return radiobrowser::Error{radiobrowser::ErrorCode::Network, "offline", 0};
  }
};

}  // namespace

int main() {
  radiobrowser::ClientOptions options;
  options.user_agent = "radiobrowser-consumer/1.0";
  options.transport = std::make_shared<OfflineTransport>();
  options.discovery = std::make_shared<radiobrowser::StaticServerDiscovery>(
      std::vector<std::string>{"https://mirror.example"});

  auto client = radiobrowser::Client::create(std::move(options));
  if (!client) {
    std::cerr << radiobrowser::to_string(client.error()) << '\n';
    return EXIT_FAILURE;
  }
  const auto stats = client->stats();
  if (!stats || stats->status != "OK") {
    std::cerr << "unexpected stats result\n";
    return EXIT_FAILURE;
  }

#if RADIOBROWSER_HAS_CURL
  // Make sure the libcurl transport links.
  const radiobrowser::CurlTransport transport;
  static_cast<void>(transport);
#endif

  std::cout << "radiobrowser " << radiobrowser::kVersion << " works\n";
  return EXIT_SUCCESS;
}
