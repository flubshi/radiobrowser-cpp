// SPDX-License-Identifier: MIT

#include "detail/hostname.hpp"
#include "detail/url.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace radiobrowser::detail {
namespace {

TEST(PercentEncodeTest, KeepsUnreservedCharacters) {
  EXPECT_EQ(percent_encode("AZaz09-._~"), "AZaz09-._~");
}

TEST(PercentEncodeTest, EncodesReservedAndUnicode) {
  EXPECT_EQ(percent_encode("smooth jazz"), "smooth%20jazz");
  EXPECT_EQ(percent_encode("a/b?c&d=e#f"), "a%2Fb%3Fc%26d%3De%23f");
  EXPECT_EQ(percent_encode("\xC3\x96sterreich"), "%C3%96sterreich");
  EXPECT_EQ(percent_encode(""), "");
}

TEST(QueryBuilderTest, BuildsQueryString) {
  QueryBuilder builder;
  EXPECT_EQ(builder.str(), "");
  builder.add("name", "Radio Bob!")
      .add_bool("reverse", true)
      .add_number("limit", 25)
      .add_if_not_empty("tag", "")
      .add_if_not_empty("codec", "MP3");
  EXPECT_EQ(builder.str(), "?name=Radio%20Bob%21&reverse=true&limit=25&codec=MP3");
}

TEST(JoinCommaTest, SkipsEmptyValues) {
  EXPECT_EQ(join_comma({"a", "", "b"}), "a,b");
  EXPECT_EQ(join_comma({}), "");
}

TEST(HostnameTest, Normalizes) {
  EXPECT_EQ(normalize_hostname("DE1.API.Radio-Browser.info."), "de1.api.radio-browser.info");
  EXPECT_EQ(normalize_hostname("fi1.api.radio-browser.info"), "fi1.api.radio-browser.info");
}

TEST(HostnameTest, RejectsInvalidNames) {
  EXPECT_FALSE(normalize_hostname(""));
  EXPECT_FALSE(normalize_hostname("."));
  EXPECT_FALSE(normalize_hostname("evil.com/path"));
  EXPECT_FALSE(normalize_hostname("evil.com:8080"));
  EXPECT_FALSE(normalize_hostname("a b.com"));
  EXPECT_FALSE(normalize_hostname("-leading.example"));
}

TEST(HostnameTest, ConvertsToBaseUrlsWithSuffixFilter) {
  const std::vector<std::string> hosts{"de1.api.radio-browser.info",
                                       "DE1.api.radio-browser.info.",
                                       "fi1.api.radio-browser.info",
                                       "api.radio-browser.info",
                                       "evil.example.com",
                                       "x.api.radio-browser.info.evil",
                                       "bad host"};
  const auto urls = hostnames_to_base_urls(hosts, ".api.radio-browser.info");
  const std::vector<std::string> expected{"https://de1.api.radio-browser.info",
                                          "https://fi1.api.radio-browser.info"};
  EXPECT_EQ(urls, expected);
}

TEST(HostnameTest, EmptySuffixAcceptsAllValidNames) {
  const auto urls = hostnames_to_base_urls({"mirror.example.org", "bad name"}, "");
  ASSERT_EQ(urls.size(), 1U);
  EXPECT_EQ(urls.front(), "https://mirror.example.org");
}

TEST(BaseUrlTest, Normalizes) {
  EXPECT_EQ(normalize_base_url("https://de1.api.radio-browser.info/"),
            "https://de1.api.radio-browser.info");
  EXPECT_EQ(normalize_base_url("http://127.0.0.1:8080"), "http://127.0.0.1:8080");
  EXPECT_EQ(normalize_base_url("HTTPS://Example.org///"), "HTTPS://Example.org");
}

TEST(BaseUrlTest, RejectsInvalidUrls) {
  EXPECT_FALSE(normalize_base_url(""));
  EXPECT_FALSE(normalize_base_url("de1.api.radio-browser.info"));
  EXPECT_FALSE(normalize_base_url("ftp://example.org"));
  EXPECT_FALSE(normalize_base_url("https://"));
  EXPECT_FALSE(normalize_base_url("https://exa mple.org"));
  EXPECT_FALSE(normalize_base_url("https://example.org?x=1"));
  EXPECT_FALSE(normalize_base_url("https://example.org\r\nHost: evil"));
}

}  // namespace
}  // namespace radiobrowser::detail
