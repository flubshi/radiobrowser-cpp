// SPDX-License-Identifier: MIT
// radiobrowser-cli: a small command line client demonstrating the library.
//
//   radiobrowser-cli servers
//   radiobrowser-cli search <name> [limit]
//   radiobrowser-cli tag <tag> [limit]
//   radiobrowser-cli top [limit]
//   radiobrowser-cli station <uuid>
//   radiobrowser-cli play <uuid>        (counts a click and prints the stream URL)
//   radiobrowser-cli countries
//   radiobrowser-cli stats
//
// Set RADIOBROWSER_VERBOSE=1 to see the library's log messages.

#include <radiobrowser/radiobrowser.hpp>

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace rb = radiobrowser;

void print_usage() {
  std::cerr << "usage: radiobrowser-cli <command> [arguments]\n"
               "\n"
               "commands:\n"
               "  servers                 list the discovered API servers\n"
               "  search <name> [limit]   search stations by name\n"
               "  tag <tag> [limit]       search stations by tag\n"
               "  top [limit]             most clicked stations\n"
               "  station <uuid>          show one station\n"
               "  play <uuid>             count a click and print the stream URL\n"
               "  countries               list countries\n"
               "  stats                   show server statistics\n";
}

std::size_t parse_limit(const std::vector<std::string>& args, std::size_t index,
                        std::size_t fallback) {
  if (index >= args.size()) {
    return fallback;
  }
  try {
    const unsigned long value = std::stoul(args[index]);  // NOLINT(google-runtime-int)
    return value == 0 ? fallback : static_cast<std::size_t>(value);
  } catch (const std::exception&) {
    return fallback;
  }
}

std::string join(const std::vector<std::string>& values) {
  std::string joined;
  for (const auto& value : values) {
    if (!joined.empty()) {
      joined += ", ";
    }
    joined += value;
  }
  return joined;
}

void print_stations(const std::vector<rb::Station>& stations) {
  for (const auto& station : stations) {
    std::cout << station.station_uuid << "  " << station.name << "  [" << station.country_code
              << ", " << station.codec << " " << station.bitrate << " kbit/s, " << station.votes
              << " votes]\n";
  }
  std::cout << stations.size() << " station(s)\n";
}

void print_station_details(const rb::Station& station) {
  std::cout << "name:       " << station.name << '\n'
            << "uuid:       " << station.station_uuid << '\n'
            << "stream:     " << station.stream_url() << '\n'
            << "homepage:   " << station.homepage << '\n'
            << "country:    " << station.country << " (" << station.country_code << ")\n"
            << "languages:  " << join(station.languages) << '\n'
            << "tags:       " << join(station.tags) << '\n'
            << "codec:      " << station.codec << ' ' << station.bitrate << " kbit/s\n"
            << "votes:      " << station.votes << '\n'
            << "clicks:     " << station.click_count << '\n'
            << "last check: " << (station.last_check_ok ? "ok" : "failed");
  if (station.last_check_time) {
    std::cout << " at " << rb::format_timestamp(*station.last_check_time);
  }
  std::cout << '\n';
}

int fail(const rb::Error& error) {
  std::cerr << "error: " << rb::to_string(error) << '\n';
  return EXIT_FAILURE;
}

int run(const std::vector<std::string>& args) {
  if (args.empty()) {
    print_usage();
    return EXIT_FAILURE;
  }

  rb::ClientOptions options;
  options.user_agent = std::string("radiobrowser-cli/") + rb::kVersion;
  const char* verbose = std::getenv("RADIOBROWSER_VERBOSE");  // NOLINT(concurrency-mt-unsafe)
  if (verbose != nullptr && std::string_view(verbose) == "1") {
    options.logger = [](rb::LogLevel level, std::string_view message) {
      std::cerr << '[' << rb::to_string(level) << "] " << message << '\n';
    };
  }
  auto created = rb::Client::create(std::move(options));
  if (!created) {
    return fail(created.error());
  }
  const rb::Client& client = *created;
  const std::string& command = args.front();

  if (command == "servers") {
    const auto servers = client.servers();
    if (!servers) {
      return fail(servers.error());
    }
    for (const auto& server : *servers) {
      std::cout << server << '\n';
    }
    return EXIT_SUCCESS;
  }

  if ((command == "search" || command == "tag") && args.size() >= 2) {
    rb::StationSearch search;
    if (command == "search") {
      search.name = args[1];
    } else {
      search.tag = args[1];
    }
    search.order = rb::StationOrder::ClickCount;
    search.reverse = true;
    search.limit = parse_limit(args, 2, 20);
    const auto stations = client.search_stations(search);
    if (!stations) {
      return fail(stations.error());
    }
    print_stations(*stations);
    return EXIT_SUCCESS;
  }

  if (command == "top") {
    rb::PageQuery query;
    query.limit = parse_limit(args, 1, 20);
    const auto stations = client.top_clicked(query);
    if (!stations) {
      return fail(stations.error());
    }
    print_stations(*stations);
    return EXIT_SUCCESS;
  }

  if (command == "station" && args.size() >= 2) {
    const auto station = client.station(args[1]);
    if (!station) {
      return fail(station.error());
    }
    print_station_details(*station);
    return EXIT_SUCCESS;
  }

  if (command == "play" && args.size() >= 2) {
    const auto click = client.count_click(args[1]);
    if (!click) {
      return fail(click.error());
    }
    std::cout << click->name << '\n' << click->url << '\n';
    return EXIT_SUCCESS;
  }

  if (command == "countries") {
    rb::ListQuery query;
    query.order = rb::ListOrder::StationCount;
    query.reverse = true;
    const auto countries = client.countries(query);
    if (!countries) {
      return fail(countries.error());
    }
    for (const auto& country : *countries) {
      std::cout << country.iso_3166_1 << "  " << country.name << "  " << country.station_count
                << '\n';
    }
    return EXIT_SUCCESS;
  }

  if (command == "stats") {
    const auto stats = client.stats();
    if (!stats) {
      return fail(stats.error());
    }
    std::cout << "status:          " << stats->status << '\n'
              << "software:        " << stats->software_version << '\n'
              << "stations:        " << stats->stations << '\n'
              << "broken stations: " << stats->stations_broken << '\n'
              << "clicks (24h):    " << stats->clicks_last_day << '\n';
    return EXIT_SUCCESS;
  }

  print_usage();
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    return run(args);
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
