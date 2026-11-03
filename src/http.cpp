// SPDX-License-Identifier: MIT

#include <radiobrowser/http.hpp>

namespace radiobrowser {

// Out-of-line so that the vtable and type information are emitted once.
HttpTransport::~HttpTransport() = default;

}  // namespace radiobrowser
