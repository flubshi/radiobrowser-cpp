# radiobrowser-cpp

[![CI](https://github.com/flubshi/radiobrowser-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/flubshi/radiobrowser-cpp/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A modern, thread-safe C++ client library for the
[radio-browser.info](https://api.radio-browser.info/) API, the free and open
directory of internet radio stations.

- **Follows the API's rules out of the box:** finds servers at runtime (DNS,
  with an HTTP fallback), randomises them, fails over to the next server,
  sends a speaking User-Agent and offers click counting.
- **Bounded and cancellable:** every call has one deadline that covers
  discovery and all failover attempts, and can be cancelled from another
  thread.
- **Pluggable HTTP:** a libcurl transport with connection reuse is included
  and optional. Applications with their own network stack implement one small
  interface ([guide](docs/custom-transport.md)).
- **No exceptions across the API:** results are returned as
  `radiobrowser::Result<T>` (modelled after `std::expected`).
- **Tolerant parsing:** the user maintained data is messy; missing fields,
  `null`s, numbers as strings and similar quirks are handled.
- **Easy to consume:** C++17 public headers (C++20 inside), CMake package with
  `find_package` and `FetchContent` support, pkg-config file, static or
  shared, MIT licensed.
- **Verified:** extensive unit tests without network access, CI on Linux
  (GCC, Clang) and Windows (MSVC), clang-tidy, clang-format and a warning-free,
  complete API documentation enforced.

## Quick start

```cpp
#include <radiobrowser/radiobrowser.hpp>
#include <iostream>

int main() {
  radiobrowser::ClientOptions options;
  options.user_agent = "myplayer/1.0";  // identify your application

  auto client = radiobrowser::Client::create(std::move(options));
  if (!client) {
    std::cerr << radiobrowser::to_string(client.error()) << '\n';
    return 1;
  }

  radiobrowser::StationSearch search;
  search.tag = "jazz";
  search.country_code = "DE";
  search.order = radiobrowser::StationOrder::ClickCount;
  search.reverse = true;
  search.limit = 10;

  const auto stations = client->search_stations(search);
  if (!stations) {
    std::cerr << radiobrowser::to_string(stations.error()) << '\n';
    return 1;
  }
  for (const auto& station : *stations) {
    std::cout << station.name << " -> " << station.stream_url() << '\n';
  }
}
```

The repository also contains a small command line client in
[`examples/cli`](examples/cli/main.cpp):

```sh
radiobrowser-cli search "radio bob" 5
radiobrowser-cli play 960e57c5-0601-11e8-ae97-52543be04c81
```

[`examples/caching_transport`](examples/caching_transport/main.cpp) shows how
to extend the library with a custom transport.

## Integration

Requirements: CMake 3.21+ and a C++20 compiler to build the library (tested
with GCC 13, Clang 18 and MinGW-w64 GCC 13; CI also builds with MSVC); code
that includes the headers only needs C++17.

Dependencies: [nlohmann/json](https://github.com/nlohmann/json) (private,
header-only) and optionally [libcurl](https://curl.se/libcurl/). Tests use
GoogleTest and cpp-httplib. Missing dependencies are downloaded with
FetchContent unless `RADIOBROWSER_FETCH_DEPS=OFF`.

### FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
  radiobrowser
  GIT_REPOSITORY https://github.com/flubshi/radiobrowser-cpp.git
  GIT_TAG v0.1.0)
FetchContent_MakeAvailable(radiobrowser)

target_link_libraries(my_app PRIVATE radiobrowser::radiobrowser)
```

### Installed package

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /usr/local
```

```cmake
find_package(radiobrowser CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE radiobrowser::radiobrowser)
```

Build systems other than CMake can use the installed pkg-config file:
`pkg-config --cflags --libs radiobrowser`.

### Build options

| Option | Default | Description |
| --- | --- | --- |
| `RADIOBROWSER_WITH_CURL` | `ON` | Build `radiobrowser::CurlTransport` (needs libcurl). |
| `RADIOBROWSER_FETCH_DEPS` | `ON` | Download missing dependencies with FetchContent. Turn off for offline and distribution builds. |
| `RADIOBROWSER_BUILD_TESTS` | top-level only | Build the unit tests. |
| `RADIOBROWSER_BUILD_LIVE_TESTS` | `OFF` | Build tests that call the real API. |
| `RADIOBROWSER_BUILD_EXAMPLES` | top-level only | Build the example CLI (needs libcurl). |
| `RADIOBROWSER_INSTALL` | top-level only | Generate install rules and the CMake package. |
| `RADIOBROWSER_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors (CI). |
| `RADIOBROWSER_CLANG_TIDY` | `OFF` | Run clang-tidy while compiling the library. |
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library. Static builds are position independent. |

`RADIOBROWSER_HAS_CURL` in `<radiobrowser/config.hpp>` tells consumers whether
the libcurl transport is available.

## Usage

### Clients, results and errors

`Client::create()` validates the options and returns `Result<Client>`. Every
call returns `Result<T>`: check it with `if (result)`, access the value with
`*result`/`->` and the failure with `result.error()`. Errors carry an
`ErrorCode` (`Network`, `Timeout`, `Cancelled`, `HttpStatus`,
`InvalidResponse`, `NotFound`, `AllServersFailed`, ...), a message, and the
HTTP status where applicable. The library never throws; only accessing the
wrong alternative of a `Result` throws `BadResultAccess`.

### Playing a station: count the click

The API asks clients to report every playback. `count_click()` does that and
returns the resolved stream URL:

```cpp
const auto click = client->count_click(station.station_uuid);
if (click) {
  play(click->url);
}
```

### Deadlines and cancellation

`ClientOptions::timeout` (default 20 s) bounds a whole call including
discovery and failover; `attempt_timeout` bounds a single server attempt.
Both can be overridden per call:

```cpp
radiobrowser::CancellationSource cancel;  // e.g. owned by a window or session

radiobrowser::RequestOptions request;
request.timeout = std::chrono::seconds(5);
request.cancellation = cancel.token();

auto countries = client->countries({}, request);
// From another thread: cancel.cancel();  -> ErrorCode::Cancelled
```

### Paging

Station lists default to `ClientOptions::default_limit` (100) entries; use
`offset` and `limit` to page. Lists of countries, languages and similar are
unlimited unless a limit is set. `hide_broken` defaults to `true` (stations
whose last check failed are skipped).

### Server discovery

By default the client resolves `all.api.radio-browser.info` via DNS, maps the
addresses back to host names via reverse DNS (needed for TLS), and falls back
to asking `https://all.api.radio-browser.info/json/servers` through the HTTP
transport. The list is shuffled, cached for `server_list_ttl` (1 h) and
refreshed after all servers failed. Alternatives:

```cpp
// Only HTTP based discovery through the configured transport:
options.discovery = std::make_shared<radiobrowser::HttpServerDiscovery>();
// A fixed server or a self-hosted mirror:
options.discovery = std::make_shared<radiobrowser::StaticServerDiscovery>(
    std::vector<std::string>{"https://de1.api.radio-browser.info"});
```

### HTTP transport

`CurlTransport` is used when no transport is configured. It verifies TLS
certificates, never downgrades HTTPS on redirects, accepts compressed
responses and keeps idle connections for reuse, so consecutive calls skip the
TCP and TLS handshakes. `CurlTransportOptions` sets a CA bundle (needed on
platforms where libcurl has no system CA store, such as Android), a proxy,
the redirect policy and the number of pooled handles.

To use another network stack, or to add behaviour such as caching, implement
`radiobrowser::HttpTransport`. The [transport guide](docs/custom-transport.md)
describes the contract; [`examples/caching_transport`](examples/caching_transport/main.cpp)
is a complete decorator that caches responses.

### Logging

The library is silent by default. Install a callback to see failovers and
discovery results:

```cpp
options.logger = [](radiobrowser::LogLevel level, std::string_view message) {
  std::clog << radiobrowser::to_string(level) << ": " << message << '\n';
};
```

### Thread safety

`Client` is thread-safe: create one instance and share it. All its member
functions are `const` and may run concurrently. Only one discovery runs at a
time; concurrent calls wait for it. The logger and custom transports or
discoveries are called from the calling threads and must be thread-safe.

## API coverage

| Endpoint | Client function |
| --- | --- |
| `/json/stations/search` | `search_stations()` |
| `/json/stations` | `stations()` |
| `/json/stations/by{name,codec,country,countrycode,state,language,tag}[exact]/<term>` | `stations_by()` |
| `/json/stations/byuuid` | `stations_by_uuid()`, `station()` |
| `/json/stations/byurl` | `stations_by_url()` |
| `/json/stations/byserveruuid` | `stations_by_server_uuid()` |
| `/json/stations/{topclick,topvote,lastclick,lastchange,broken}` | `top_clicked()`, `top_voted()`, `recently_clicked()`, `recently_changed()`, `broken_stations()` |
| `/json/stations/changed` | `station_changes()` |
| `/json/{countries,countrycodes,codecs,states,languages,tags}` | `countries()`, `country_codes()`, `codecs()`, `states()`, `languages()`, `tags()` |
| `/json/url/<uuid>` | `count_click()` |
| `/json/vote/<uuid>` | `vote()` |
| `/json/checks`, `/json/checksteps`, `/json/clicks` | `checks()`, `check_steps()`, `clicks()` |
| `/json/streamingservers` | `streaming_servers()`, `streaming_servers_by_uuid()`, `streaming_servers_by_station()` |
| `/json/stats`, `/json/config`, `/json/servers` | `stats()`, `config()`, `server_mirrors()` |
| anything else | `get_raw()` |

Adding stations (`/json/add`) is not supported yet.

## Building and testing

```sh
cmake --preset dev            # Debug, warnings as errors, compile_commands.json
cmake --build --preset dev
ctest --preset dev            # unit tests, no network access needed
```

Without presets:

```sh
cmake -S . -B build -DRADIOBROWSER_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --label-regex unit --output-on-failure
```

The unit tests cover the JSON mapping with recorded fixtures (including
malformed and hostile input), request building, failover, deadlines,
cancellation and thread safety with fake transports, and the libcurl
transport against a local HTTP server started inside the test process. Tests
against the real API are separate:

```sh
cmake --preset live && cmake --build --preset live && ctest --preset live
```

They never count clicks or votes.

On Windows, dependencies come from vcpkg (`vcpkg.json`):

```sh
cmake --preset windows-vcpkg      # needs VCPKG_ROOT
cmake --build --preset windows-vcpkg
ctest --preset windows-vcpkg
```

Code style: `clang-format` (config in `.clang-format`) and `clang-tidy`
(`.clang-tidy`, run it with `cmake --preset tidy && cmake --build --preset tidy`).
CI checks both and builds on Linux (GCC, Clang, static, shared, without
libcurl) and Windows (MSVC). The live tests run in every pull request but do
not block it.

The API reference is generated with Doxygen (`mkdir -p build/docs && doxygen
docs/Doxyfile`); CI fails on undocumented public API and publishes the
reference to GitHub Pages for releases.

## Contributing and releases

Contributions are welcome, see [CONTRIBUTING.md](CONTRIBUTING.md). Pull
request titles follow [Conventional Commits](https://www.conventionalcommits.org/)
(`feat: ...`, `fix: ...`); releases, version numbers (SemVer) and the
[changelog](CHANGELOG.md) are created by release-please from them.

Please report security issues privately, see [SECURITY.md](SECURITY.md).

## License

MIT, see [LICENSE](LICENSE). This project is not affiliated with
radio-browser.info. Please respect the
[API's usage notes](https://api.radio-browser.info/): send a speaking
User-Agent and count clicks.
