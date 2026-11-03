// SPDX-License-Identifier: MIT
// Compiled as C++17 to guarantee that the public headers stay usable by
// projects that build with C++17.

#include <radiobrowser/radiobrowser.hpp>

#if defined(_MSVC_LANG)
static_assert(_MSVC_LANG < 202002L, "this file must be compiled as C++17");
#else
static_assert(__cplusplus < 202002L, "this file must be compiled as C++17");
#endif

namespace radiobrowser_cpp17_check {

radiobrowser::Result<radiobrowser::Client> create_client();

radiobrowser::Result<radiobrowser::Client> create_client() {
  radiobrowser::ClientOptions options;
  options.user_agent = "cpp17-check/1.0";
  options.discovery = std::make_shared<radiobrowser::StaticServerDiscovery>(
      std::vector<std::string>{"https://de1.api.radio-browser.info"});
  return radiobrowser::Client::create(std::move(options));
}

}  // namespace radiobrowser_cpp17_check
