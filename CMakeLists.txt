cmake_minimum_required(VERSION 3.21)

project(
  radiobrowser
  VERSION 0.1.0 # x-release-please-version
  DESCRIPTION "C++ client library for the radio-browser.info API"
  HOMEPAGE_URL "https://github.com/flubshi/radiobrowser-cpp"
  LANGUAGES CXX)

# --- Options -------------------------------------------------------------------

option(RADIOBROWSER_WITH_CURL "Build the libcurl based HTTP transport" ON)
option(RADIOBROWSER_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(RADIOBROWSER_BUILD_LIVE_TESTS "Build tests that call the real radio-browser API" OFF)
option(RADIOBROWSER_BUILD_EXAMPLES "Build the example programs" ${PROJECT_IS_TOP_LEVEL})
option(RADIOBROWSER_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(RADIOBROWSER_FETCH_DEPS "Download missing dependencies with FetchContent" ON)
option(RADIOBROWSER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(RADIOBROWSER_CLANG_TIDY "Run clang-tidy on the library sources while compiling" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

if(RADIOBROWSER_WITH_CURL)
  set(RADIOBROWSER_HAS_CURL 1)
else()
  set(RADIOBROWSER_HAS_CURL 0)
endif()

# --- Dependencies ----------------------------------------------------------------

find_package(Threads REQUIRED)
radiobrowser_find_nlohmann_json()
if(RADIOBROWSER_WITH_CURL)
  find_package(CURL REQUIRED)
endif()

# --- Library ---------------------------------------------------------------------

add_library(radiobrowser)
add_library(radiobrowser::radiobrowser ALIAS radiobrowser)

target_sources(
  radiobrowser
  PRIVATE src/client.cpp
          src/discovery.cpp
          src/http.cpp
          src/timestamp.cpp
          src/detail/hostname.cpp
          src/detail/json_mapping.cpp
          src/detail/system_resolver.cpp
          src/detail/url.cpp)
if(RADIOBROWSER_WITH_CURL)
  target_sources(radiobrowser PRIVATE src/curl_transport.cpp)
endif()

set(RADIOBROWSER_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/include")
configure_file(cmake/config.hpp.in "${RADIOBROWSER_GENERATED_INCLUDE_DIR}/radiobrowser/config.hpp"
               @ONLY)
include(GenerateExportHeader)
generate_export_header(
  radiobrowser BASE_NAME RADIOBROWSER EXPORT_FILE_NAME
  "${RADIOBROWSER_GENERATED_INCLUDE_DIR}/radiobrowser/export.hpp")

include(GNUInstallDirs)
target_include_directories(
  radiobrowser
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<BUILD_INTERFACE:${RADIOBROWSER_GENERATED_INCLUDE_DIR}>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${PROJECT_SOURCE_DIR}/src)

# Consumers only need C++17; the implementation uses C++20.
target_compile_features(radiobrowser PUBLIC cxx_std_17)

if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(RADIOBROWSER_SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
else()
  set(RADIOBROWSER_SOVERSION "${PROJECT_VERSION_MAJOR}")
endif()

set_target_properties(
  radiobrowser
  PROPERTIES CXX_STANDARD 20
             CXX_STANDARD_REQUIRED ON
             CXX_EXTENSIONS OFF
             # Lets the static library be linked into shared libraries and plugins.
             POSITION_INDEPENDENT_CODE ON
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
             VERSION ${PROJECT_VERSION}
             SOVERSION ${RADIOBROWSER_SOVERSION}
             EXPORT_NAME radiobrowser)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(radiobrowser PUBLIC RADIOBROWSER_STATIC_DEFINE)
endif()

# nlohmann/json is header-only and an implementation detail: BUILD_INTERFACE
# keeps it out of the exported link interface.
target_link_libraries(radiobrowser PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
                                           Threads::Threads)
if(RADIOBROWSER_WITH_CURL)
  target_link_libraries(radiobrowser PRIVATE CURL::libcurl)
endif()
if(WIN32)
  target_link_libraries(radiobrowser PRIVATE ws2_32)
  target_compile_definitions(radiobrowser PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

radiobrowser_set_warnings(radiobrowser STRICT)

if(RADIOBROWSER_CLANG_TIDY)
  find_program(RADIOBROWSER_CLANG_TIDY_EXE NAMES clang-tidy-18 clang-tidy REQUIRED)
  set_target_properties(radiobrowser PROPERTIES CXX_CLANG_TIDY "${RADIOBROWSER_CLANG_TIDY_EXE}")
endif()

# --- Tests and examples ----------------------------------------------------------

if(RADIOBROWSER_BUILD_TESTS OR RADIOBROWSER_BUILD_LIVE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(RADIOBROWSER_BUILD_EXAMPLES)
  if(RADIOBROWSER_WITH_CURL)
    add_subdirectory(examples)
  else()
    message(STATUS "radiobrowser: examples need RADIOBROWSER_WITH_CURL, skipping them")
  endif()
endif()

# --- Installation ----------------------------------------------------------------

if(RADIOBROWSER_INSTALL)
  include(CMakePackageConfigHelpers)

  set(RADIOBROWSER_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/radiobrowser")

  install(
    TARGETS radiobrowser
    EXPORT radiobrowserTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  set(RADIOBROWSER_HEADER_EXCLUDES)
  if(NOT RADIOBROWSER_WITH_CURL)
    list(APPEND RADIOBROWSER_HEADER_EXCLUDES PATTERN "curl_transport.hpp" EXCLUDE)
  endif()
  install(
    DIRECTORY include/radiobrowser
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
    PATTERN "*.hpp" ${RADIOBROWSER_HEADER_EXCLUDES})
  install(FILES "${RADIOBROWSER_GENERATED_INCLUDE_DIR}/radiobrowser/config.hpp"
                "${RADIOBROWSER_GENERATED_INCLUDE_DIR}/radiobrowser/export.hpp"
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/radiobrowser)

  install(
    EXPORT radiobrowserTargets
    NAMESPACE radiobrowser::
    DESTINATION ${RADIOBROWSER_CMAKE_DIR})

  if(BUILD_SHARED_LIBS)
    set(RADIOBROWSER_IS_STATIC OFF)
  else()
    set(RADIOBROWSER_IS_STATIC ON)
  endif()
  configure_package_config_file(
    cmake/radiobrowserConfig.cmake.in "${PROJECT_BINARY_DIR}/radiobrowserConfig.cmake"
    INSTALL_DESTINATION ${RADIOBROWSER_CMAKE_DIR})
  # The API may change between 0.x minor versions.
  write_basic_package_version_file("${PROJECT_BINARY_DIR}/radiobrowserConfigVersion.cmake"
                                   COMPATIBILITY SameMinorVersion)
  install(FILES "${PROJECT_BINARY_DIR}/radiobrowserConfig.cmake"
                "${PROJECT_BINARY_DIR}/radiobrowserConfigVersion.cmake"
          DESTINATION ${RADIOBROWSER_CMAKE_DIR})

  # pkg-config file for build systems other than CMake. A static library needs
  # its dependencies at link time, so they are public there and private for
  # the shared library.
  set(RADIOBROWSER_PC_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
  set(RADIOBROWSER_PC_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
  set(RADIOBROWSER_PC_CURL "")
  if(RADIOBROWSER_WITH_CURL)
    set(RADIOBROWSER_PC_CURL "libcurl")
  endif()
  set(RADIOBROWSER_PC_SYSTEM_LIBS "")
  if(WIN32)
    set(RADIOBROWSER_PC_SYSTEM_LIBS " -lws2_32")
  elseif(CMAKE_THREAD_LIBS_INIT)
    set(RADIOBROWSER_PC_SYSTEM_LIBS " ${CMAKE_THREAD_LIBS_INIT}")
  endif()
  if(BUILD_SHARED_LIBS)
    set(RADIOBROWSER_PC_REQUIRES "")
    set(RADIOBROWSER_PC_REQUIRES_PRIVATE "${RADIOBROWSER_PC_CURL}")
    set(RADIOBROWSER_PC_CFLAGS "")
    set(RADIOBROWSER_PC_LIBS "")
    string(STRIP "${RADIOBROWSER_PC_SYSTEM_LIBS}" RADIOBROWSER_PC_LIBS_PRIVATE)
  else()
    set(RADIOBROWSER_PC_REQUIRES "${RADIOBROWSER_PC_CURL}")
    set(RADIOBROWSER_PC_REQUIRES_PRIVATE "")
    set(RADIOBROWSER_PC_CFLAGS " -DRADIOBROWSER_STATIC_DEFINE")
    set(RADIOBROWSER_PC_LIBS "${RADIOBROWSER_PC_SYSTEM_LIBS}")
    set(RADIOBROWSER_PC_LIBS_PRIVATE "")
  endif()
  configure_file(cmake/radiobrowser.pc.in "${PROJECT_BINARY_DIR}/radiobrowser.pc" @ONLY)
  install(FILES "${PROJECT_BINARY_DIR}/radiobrowser.pc"
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
  install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
