# Dependency lookup: installed packages first, FetchContent as a fallback.
#
# Set RADIOBROWSER_FETCH_DEPS=OFF for offline, sandboxed and distribution
# builds; missing dependencies then fail at configure time.

include(FetchContent)

# Avoid the DOWNLOAD_EXTRACT_TIMESTAMP warning of newer CMake versions.
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Fetched dependencies are third-party code: mark their headers as system
# headers so our warning flags do not apply to them (needs CMake 3.25).
set(RADIOBROWSER_FETCH_SYSTEM)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
  set(RADIOBROWSER_FETCH_SYSTEM SYSTEM)
endif()

macro(radiobrowser_require_fetch name)
  if(NOT RADIOBROWSER_FETCH_DEPS)
    message(
      FATAL_ERROR
        "radiobrowser: ${name} was not found and RADIOBROWSER_FETCH_DEPS is OFF. "
        "Install it or set CMAKE_PREFIX_PATH.")
  endif()
  message(STATUS "radiobrowser: ${name} not found, downloading it with FetchContent")
endmacro()

# nlohmann/json (header-only, private to the library)
macro(radiobrowser_find_nlohmann_json)
  if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.10 CONFIG QUIET)
    if(nlohmann_json_FOUND)
      message(STATUS "radiobrowser: using nlohmann_json ${nlohmann_json_VERSION}")
    else()
      radiobrowser_require_fetch(nlohmann_json)
      set(JSON_Install OFF CACHE INTERNAL "")
      set(JSON_BuildTests OFF CACHE INTERNAL "")
      FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        ${RADIOBROWSER_FETCH_SYSTEM})
      FetchContent_MakeAvailable(nlohmann_json)
    endif()
  endif()
endmacro()

# GoogleTest including gMock (tests only)
macro(radiobrowser_find_gtest)
  if(NOT TARGET GTest::gmock)
    find_package(GTest 1.11 CONFIG QUIET)
    if(GTest_FOUND AND TARGET GTest::gmock)
      message(STATUS "radiobrowser: using GoogleTest ${GTest_VERSION}")
    else()
      radiobrowser_require_fetch(GoogleTest)
      set(INSTALL_GTEST OFF CACHE INTERNAL "")
      set(BUILD_GMOCK ON CACHE INTERNAL "")
      # Use the same runtime library as the rest of the build on MSVC.
      set(gtest_force_shared_crt ON CACHE INTERNAL "")
      FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723 # v1.17.0
        ${RADIOBROWSER_FETCH_SYSTEM})
      FetchContent_MakeAvailable(googletest)
    endif()
  endif()
endmacro()

# cpp-httplib (tests only: local HTTP server for the libcurl transport tests)
macro(radiobrowser_find_httplib)
  if(NOT TARGET httplib::httplib)
    find_package(httplib CONFIG QUIET)
    if(httplib_FOUND)
      message(STATUS "radiobrowser: using cpp-httplib ${httplib_VERSION}")
    else()
      radiobrowser_require_fetch(cpp-httplib)
      set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE INTERNAL "")
      set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE INTERNAL "")
      set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE INTERNAL "")
      set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE INTERNAL "")
      set(HTTPLIB_INSTALL OFF CACHE INTERNAL "")
      set(HTTPLIB_COMPILE OFF CACHE INTERNAL "")
      FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG 787a34ad7f01f20922a237d5142aae469828be72 # v0.20.0
        ${RADIOBROWSER_FETCH_SYSTEM})
      FetchContent_MakeAvailable(httplib)
    endif()
  endif()
endmacro()
