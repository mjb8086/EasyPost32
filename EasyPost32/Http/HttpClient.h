#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <stop_token>
#include <utility>
#include <vector>

namespace easypost
{
using Header = std::pair<std::string, std::string>;

struct HttpRequest
{
    std::string method = "GET";
    std::string url;
    std::vector<Header> headers;
    std::optional<std::string> body; // Exact bytes, including embedded NULs.
};

struct HttpOptions
{
    std::chrono::milliseconds connectTimeout{ 5000 };
    std::chrono::milliseconds totalTimeout{ 30000 };
    std::size_t maxResponseBytes = 16 * 1024 * 1024;
    std::size_t maxHeaderBytes = 256 * 1024;
    int maxRedirects = 5; // Same-origin HTTP(S) redirects only.
    bool verifyTls = true; // Controls certificate chain AND hostname verification.
    std::string caFile; // Optional PEM trust roots; empty uses the native trust store.
    std::stop_token cancellation;
};

enum class TransportErrorKind
{
    InvalidRequest, Initialization, Connection, Timeout, Tls, RedirectLimit,
    RedirectBlocked, ResponseTooLarge, HeadersTooLarge, Cancelled, Other
};

struct TransportError
{
    TransportErrorKind kind;
    int nativeCode = 0;
    std::string message; // Generic diagnostic; never copies request data.
};

struct HttpResponse
{
    long status = 0;
    std::string statusText;
    std::string finalUrl;
    std::vector<Header> headers; // Final response block; repeated headers retained.
    std::string rawHeaders;
    std::string body; // Bytes; not necessarily text or complete if error is set.
    bool bodyIsText = true;
    bool tlsVerificationEnabled = true;
    std::chrono::milliseconds elapsed{ 0 };
    std::size_t downloadedBytes = 0; // Includes intermediate redirect bodies.
    std::size_t uploadedBytes = 0;
    int redirects = 0;
    std::optional<TransportError> error; // HTTP 4xx/5xx alone never set this.
};

bool IsTextBody(std::string_view bytes) noexcept;

// Blocking: call only from tests or a worker. Each call owns a separate easy
// handle. No UI, logging, cookie persistence or environment proxy configuration.
HttpResponse Execute(const HttpRequest& request, const HttpOptions& options = {});
}
