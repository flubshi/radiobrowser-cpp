# Changelog

All notable changes to this project are documented in this file. From version
0.1.0 on, the file is maintained by [release-please](https://github.com/googleapis/release-please)
based on [Conventional Commits](https://www.conventionalcommits.org/).

## 0.1.0 (2026-11-03)

### Features

* Thread-safe client for the radio-browser.info API covering all read
  endpoints, click counting and voting.
* Server discovery via DNS with an HTTP fallback, randomised server order,
  failover and a cached server list.
* One deadline per call across discovery and failover, cooperative
  cancellation and a logging callback.
* Pluggable HTTP transport; the included libcurl transport verifies TLS,
  never downgrades HTTPS on redirects and reuses connections.
* Tolerant JSON mapping of the user maintained station data.
* CMake package, pkg-config file, static or shared library, C++17 public
  headers.
