radiobrowser_find_gtest()
include(GoogleTest)

# The public headers must stay C++17 compatible.
add_library(radiobrowser_cpp17_header_check OBJECT support/cpp17_header_check.cpp)
target_link_libraries(radiobrowser_cpp17_header_check PRIVATE radiobrowser::radiobrowser)
set_target_properties(radiobrowser_cpp17_header_check PROPERTIES CXX_STANDARD 17
                                                                 CXX_STANDARD_REQUIRED ON
                                                                 CXX_EXTENSIONS OFF)
radiobrowser_set_warnings(radiobrowser_cpp17_header_check STRICT)

if(RADIOBROWSER_BUILD_TESTS)
  add_executable(
    radiobrowser_tests
    unit/client_test.cpp
    unit/discovery_test.cpp
    unit/json_mapping_test.cpp
    unit/result_test.cpp
    unit/timestamp_test.cpp
    unit/url_and_hostname_test.cpp)

  if(RADIOBROWSER_WITH_CURL)
    radiobrowser_find_httplib()
    target_sources(radiobrowser_tests PRIVATE unit/curl_transport_test.cpp)
    target_link_libraries(radiobrowser_tests PRIVATE httplib::httplib)
  endif()

  # Internal helpers are tested directly. A shared library hides them, so in
  # that case the test binary compiles its own copy.
  if(BUILD_SHARED_LIBS)
    target_sources(
      radiobrowser_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src/detail/hostname.cpp
              ${PROJECT_SOURCE_DIR}/src/detail/json_mapping.cpp
              ${PROJECT_SOURCE_DIR}/src/detail/url.cpp)
  endif()

  target_include_directories(radiobrowser_tests PRIVATE ${PROJECT_SOURCE_DIR}/src support)
  if(WIN32)
    target_compile_definitions(radiobrowser_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  endif()
  target_compile_definitions(radiobrowser_tests
                             PRIVATE RADIOBROWSER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  target_link_libraries(
    radiobrowser_tests PRIVATE radiobrowser::radiobrowser nlohmann_json::nlohmann_json GTest::gmock
                               GTest::gtest_main Threads::Threads)
  set_target_properties(radiobrowser_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                                                      CXX_EXTENSIONS OFF)
  radiobrowser_set_warnings(radiobrowser_tests RELAXED)

  gtest_discover_tests(
    radiobrowser_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS unit TIMEOUT 120)
endif()

if(RADIOBROWSER_BUILD_LIVE_TESTS)
  if(NOT RADIOBROWSER_WITH_CURL)
    message(FATAL_ERROR "RADIOBROWSER_BUILD_LIVE_TESTS requires RADIOBROWSER_WITH_CURL")
  endif()
  add_executable(radiobrowser_live_tests live/live_test.cpp)
  target_link_libraries(radiobrowser_live_tests PRIVATE radiobrowser::radiobrowser GTest::gtest_main)
  set_target_properties(radiobrowser_live_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                                                           CXX_EXTENSIONS OFF)
  radiobrowser_set_warnings(radiobrowser_live_tests RELAXED)
  gtest_discover_tests(
    radiobrowser_live_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS live TIMEOUT 180)
endif()
