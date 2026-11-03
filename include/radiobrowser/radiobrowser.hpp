// SPDX-License-Identifier: MIT
/// @file
/// Convenience header that includes the complete public API.

#pragma once

#include <radiobrowser/cancellation.hpp>
#include <radiobrowser/client.hpp>
#include <radiobrowser/config.hpp>
#include <radiobrowser/discovery.hpp>
#include <radiobrowser/error.hpp>
#include <radiobrowser/http.hpp>
#include <radiobrowser/log.hpp>
#include <radiobrowser/query.hpp>
#include <radiobrowser/result.hpp>
#include <radiobrowser/types.hpp>

#if RADIOBROWSER_HAS_CURL
#include <radiobrowser/curl_transport.hpp>
#endif
