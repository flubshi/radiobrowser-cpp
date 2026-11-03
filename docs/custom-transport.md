# Custom HTTP transports

All network traffic of radiobrowser-cpp goes through the
`radiobrowser::HttpTransport` interface. The library ships `CurlTransport`
(libcurl), but any HTTP client can be plugged in. Typical reasons:

- the application already has an HTTP stack with its own proxy, certificate
  and connection settings, and all traffic must go through it;
- the platform offers a native HTTP API that should be preferred over libcurl;
- behaviour should be added around the real transport, for example caching,
  metrics or request recording (decorator pattern);
- tests need deterministic responses without network access.

## The contract

A transport implements one function:

```cpp
radiobrowser::Result<radiobrowser::HttpResponse> perform(const radiobrowser::HttpRequest& request);
```

| Requirement | Details |
| --- | --- |
| Thread safety | `perform()` is called concurrently from every thread that uses the client. |
| No exceptions | Report failures as `Error` values. Exceptions are caught and reported as `ErrorCode::Internal`, but should not be relied upon. |
| HTTP status | Every completed exchange is a success: return the status code and body in `HttpResponse`, also for 4xx and 5xx. The client decides about failover. |
| Headers | Send all entries of `request.headers` (`User-Agent`, `Accept`). |
| Timeouts | Give up after `request.timeout` (whole transfer) and `request.connect_timeout`, and report `ErrorCode::Timeout`. |
| Size limit | Abort once the body exceeds `request.max_response_bytes` (0 means unlimited) and report `ErrorCode::ResponseTooLarge`. |
| Cancellation | Poll `request.cancellation.is_cancelled()` while waiting and report `ErrorCode::Cancelled`. |
| TLS | Verify certificates and host names. Do not downgrade HTTPS to HTTP on redirects. |
| Error mapping | Connection, DNS and TLS failures: `ErrorCode::Network`. |

The client retries the next server after `Network`, `Timeout` and HTTP 408,
429 and 5xx, and stops immediately after `Cancelled`, `ResponseTooLarge`,
`InvalidArgument` and `Internal`.

## Skeleton

```cpp
#include <radiobrowser/radiobrowser.hpp>

class MyTransport final : public radiobrowser::HttpTransport {
 public:
  radiobrowser::Result<radiobrowser::HttpResponse> perform(
      const radiobrowser::HttpRequest& request) override {
    using radiobrowser::Error;
    using radiobrowser::ErrorCode;

    // 1. Create the request with request.url, request.method and request.headers.
    // 2. Apply request.timeout and request.connect_timeout.
    // 3. While receiving:
    //    - return Error{ErrorCode::Cancelled, ...} once
    //      request.cancellation.is_cancelled() is true,
    //    - return Error{ErrorCode::ResponseTooLarge, ...} once the body grows
    //      beyond request.max_response_bytes (if not 0).
    // 4. Map failures: timeout -> ErrorCode::Timeout,
    //    connection/DNS/TLS -> ErrorCode::Network.
    // 5. Return radiobrowser::HttpResponse{status, body} for every response.
    return Error{ErrorCode::Internal, "not implemented", 0};
  }
};
```

Pass it to the client:

```cpp
radiobrowser::ClientOptions options;
options.user_agent = "myapp/1.0";
options.transport = std::make_shared<MyTransport>();
```

## Server discovery and the transport

The default discovery first resolves `all.api.radio-browser.info` with the
operating system's resolver and only falls back to HTTP. DNS lookups therefore
bypass a custom transport. If every connection must go through the transport,
for example because of a mandatory proxy, select a discovery that uses it:

```cpp
// Asks https://all.api.radio-browser.info/json/servers through the transport.
options.discovery = std::make_shared<radiobrowser::HttpServerDiscovery>();

// Or: a fixed list of servers, e.g. a self-hosted mirror.
options.discovery = std::make_shared<radiobrowser::StaticServerDiscovery>(
    std::vector<std::string>{"https://radio-mirror.example.org"});
```

## Decorators

A transport can wrap another transport to add behaviour without touching the
client. [`examples/caching_transport`](../examples/caching_transport/main.cpp)
is a complete, compiled example: it caches successful GET responses for a
configurable time and always forwards clicks and votes.

## Testing

Fake transports make unit tests deterministic. The library's own tests use a
thread-safe fake that records requests and answers from JSON fixtures, see
[`tests/support/test_support.hpp`](../tests/support/test_support.hpp).
