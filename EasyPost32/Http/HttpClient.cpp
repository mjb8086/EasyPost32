#include "HttpClient.h"
#include <curl/curl.h>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

namespace easypost
{
bool IsTextBody(std::string_view bytes) noexcept
{
    for (std::size_t i = 0; i < bytes.size();)
    {
        const auto first = static_cast<unsigned char>(bytes[i++]);
        if (first < 0x80)
        {
            if ((first < 0x20 && first != '\t' && first != '\r' && first != '\n') || first == 0x7f)
                return false;
            continue;
        }
        const int count = first >= 0xc2 && first <= 0xdf ? 2 :
            first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (count == 0 || bytes.size() - i < static_cast<std::size_t>(count - 1))
            return false;
        std::uint32_t point = first & (0x7f >> count);
        for (int j = 1; j < count; ++j)
        {
            const auto next = static_cast<unsigned char>(bytes[i++]);
            if ((next & 0xc0) != 0x80)
                return false;
            point = (point << 6) | (next & 0x3f);
        }
        if ((count == 3 && point < 0x800) || (count == 4 && point < 0x10000) ||
            (point >= 0xd800 && point <= 0xdfff) || point > 0x10ffff || point < 0xa0)
            return false;
    }
    return true;
}

namespace
{
struct CurlRuntime
{
    CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
    ~CurlRuntime() { if (code == CURLE_OK) curl_global_cleanup(); }
};
struct HeaderList
{
    curl_slist* value = nullptr;
    ~HeaderList() { curl_slist_free_all(value); }
    void Add(const std::string& line)
    {
        auto* next = curl_slist_append(value, line.c_str());
        if (!next) throw std::bad_alloc();
        value = next;
    }
};
struct CurlFailure { CURLcode code; };

std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return text;
}

bool Token(std::string_view text)
{
    if (text.empty()) return false;
    for (const auto c : text)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos))
            return false;
    return true;
}

std::optional<std::string> Origin(const std::string& address)
{
    if (address.find('\0') != std::string::npos) return {};
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, address.c_str(), 0) != CURLUE_OK)
        return {};
    auto part = [&url](CURLUPart which)
    {
        char* value = nullptr;
        if (curl_url_get(url.get(), which, &value, CURLU_DEFAULT_PORT) != CURLUE_OK)
            return std::string{};
        std::unique_ptr<char, decltype(&curl_free)> owned(value, curl_free);
        return Lower(value);
    };
    const auto scheme = part(CURLUPART_SCHEME);
    if (scheme != "http" && scheme != "https") return {};
    return scheme + "://" + part(CURLUPART_HOST) + ":" + part(CURLUPART_PORT);
}

std::string_view Trim(std::string_view value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
        value.back() == '\r' || value.back() == '\n')) value.remove_suffix(1);
    return value;
}

struct Capture
{
    HttpResponse& response;
    const HttpOptions& options;
    std::size_t bodyBytes = 0;
    std::size_t headerBytes = 0;
    std::optional<TransportErrorKind> failure;
};

std::size_t BodyCallback(char* data, std::size_t size, std::size_t count, void* context) noexcept
{
    auto& capture = *static_cast<Capture*>(context);
    if (count && size > (std::numeric_limits<std::size_t>::max)() / count) return 0;
    const auto bytes = size * count;
    if (bytes > capture.options.maxResponseBytes - capture.bodyBytes)
    {
        capture.failure = TransportErrorKind::ResponseTooLarge;
        return 0;
    }
    try { capture.response.body.append(data, bytes); }
    catch (...) { capture.failure = TransportErrorKind::Other; return 0; }
    capture.bodyBytes += bytes;
    return bytes;
}

std::size_t HeaderCallback(char* data, std::size_t size, std::size_t count, void* context) noexcept
{
    auto& capture = *static_cast<Capture*>(context);
    if (count && size > (std::numeric_limits<std::size_t>::max)() / count) return 0;
    const auto bytes = size * count;
    if (bytes > capture.options.maxHeaderBytes - capture.headerBytes)
    {
        capture.failure = TransportErrorKind::HeadersTooLarge;
        return 0;
    }
    capture.headerBytes += bytes;
    try
    {
        const std::string_view line(data, bytes);
        auto& result = capture.response;
        if (line.starts_with("HTTP/"))
        {
            result.headers.clear();
            result.rawHeaders.clear();
            const auto code = line.find(' ');
            const auto reason = code == line.npos ? line.npos : line.find(' ', code + 1);
            result.statusText = reason == line.npos ? "" : std::string(Trim(line.substr(reason + 1)));
        }
        else if (const auto colon = line.find(':'); colon != line.npos)
            result.headers.emplace_back(line.substr(0, colon), Trim(line.substr(colon + 1)));
        result.rawHeaders.append(data, bytes);
    }
    catch (...) { capture.failure = TransportErrorKind::Other; return 0; }
    return bytes;
}

TransportError Error(CURLcode code)
{
    auto kind = TransportErrorKind::Other;
    if (code == CURLE_OPERATION_TIMEDOUT) kind = TransportErrorKind::Timeout;
    else if (code == CURLE_COULDNT_CONNECT || code == CURLE_COULDNT_RESOLVE_HOST)
        kind = TransportErrorKind::Connection;
    else if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR ||
        code == CURLE_SSL_CACERT_BADFILE || code == CURLE_SSL_CERTPROBLEM || code == CURLE_SSL_ISSUER_ERROR)
        kind = TransportErrorKind::Tls;
    else if (code == CURLE_URL_MALFORMAT || code == CURLE_UNSUPPORTED_PROTOCOL)
        kind = TransportErrorKind::InvalidRequest;
    return { kind, static_cast<int>(code), curl_easy_strerror(code) };
}
}

HttpResponse Execute(const HttpRequest& request, const HttpOptions& options)
{
    const auto start = std::chrono::steady_clock::now();
    HttpResponse result;
    result.tlsVerificationEnabled = options.verifyTls;
    auto finish = [&]
    {
        result.bodyIsText = IsTextBody(result.body);
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        return std::move(result);
    };
    auto invalid = [&]
    {
        result.error = TransportError{ TransportErrorKind::InvalidRequest, 0,
            "Invalid HTTP method, URL, header, body or timeout/size options." };
        return finish();
    };
    const auto positiveTimeout = [](auto timeout) { return timeout.count() > 0 && timeout.count() <= LONG_MAX; };
    if (!Token(request.method) || !positiveTimeout(options.connectTimeout) ||
        !positiveTimeout(options.totalTimeout) || options.maxRedirects < 0 ||
        options.maxResponseBytes == 0 || options.maxHeaderBytes == 0 ||
        options.caFile.find('\0') != std::string::npos ||
        (request.method == "HEAD" && request.body && !request.body->empty()))
        return invalid();
    for (const auto& [name, value] : request.headers)
        if (!Token(name) || value.find_first_of(std::string_view("\r\n\0", 3)) != value.npos)
            return invalid();

    static CurlRuntime runtime;
    if (runtime.code != CURLE_OK)
    {
        result.error = TransportError{ TransportErrorKind::Initialization,
            static_cast<int>(runtime.code), "Unable to initialize libcurl." };
        return finish();
    }
    const auto origin = Origin(request.url);
    if (!origin) return invalid();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle)
    {
        result.error = TransportError{ TransportErrorKind::Initialization, 0, "Unable to create an HTTP handle." };
        return finish();
    }
    Capture capture{ result, options };
    std::string url = request.url;
    std::string method = request.method;
    std::optional<std::string_view> body;
    if (request.body) body = *request.body;
    try
    {
        HeaderList headers, bodylessHeaders;
        for (const auto& [name, value] : request.headers)
        {
            const auto line = name + (value.empty() ? ";" : ": " + value);
            headers.Add(line);
            const auto lower = Lower(name);
            if (lower != "content-length" && lower != "transfer-encoding" &&
                lower != "content-type" && lower != "content-encoding" && lower != "expect")
                bodylessHeaders.Add(line);
        }
        bool redirectedToGet = false;
        for (;;)
        {
            const auto remaining = options.totalTimeout -
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            if (remaining.count() <= 0) { result.error = Error(CURLE_OPERATION_TIMEDOUT); break; }
            result.body.clear();
            result.headers.clear();
            result.rawHeaders.clear();
            result.statusText.clear();
            result.status = 0;
            curl_easy_reset(handle.get());
            auto set = [&handle](CURLoption option, auto value)
            {
                const auto code = curl_easy_setopt(handle.get(), option, value);
                if (code != CURLE_OK) throw CurlFailure{ code };
            };
            set(CURLOPT_URL, url.c_str());
            set(CURLOPT_PROTOCOLS_STR, "http,https");
            set(CURLOPT_PROXY, ""); // Explicit direct connections, independent of proxy environment variables.
            set(CURLOPT_NOSIGNAL, 1L);
            set(CURLOPT_PATH_AS_IS, 1L);
            set(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>((std::min)(remaining, options.connectTimeout).count()));
            set(CURLOPT_TIMEOUT_MS, static_cast<long>(remaining.count()));
            set(CURLOPT_SSL_VERIFYPEER, options.verifyTls ? 1L : 0L);
            set(CURLOPT_SSL_VERIFYHOST, options.verifyTls ? 2L : 0L);
            if (!options.caFile.empty()) set(CURLOPT_CAINFO, options.caFile.c_str());
            set(CURLOPT_HTTPHEADER, redirectedToGet ? bodylessHeaders.value : headers.value);
            set(CURLOPT_WRITEFUNCTION, &BodyCallback);
            set(CURLOPT_WRITEDATA, &capture);
            set(CURLOPT_HEADERFUNCTION, &HeaderCallback);
            set(CURLOPT_HEADERDATA, &capture);
            // POSTFIELDS plus an explicit size preserves bytes, including NULs.
            if (method != "HEAD" && (body || method == "POST"))
            {
                set(CURLOPT_POSTFIELDS, body ? body->data() : "");
                set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body ? body->size() : 0));
            }
            set(CURLOPT_NOBODY, method == "HEAD" ? 1L : 0L);
            set(CURLOPT_CUSTOMREQUEST, method.c_str());
            const auto code = curl_easy_perform(handle.get());
            curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &result.status);
            char* effective = nullptr;
            curl_easy_getinfo(handle.get(), CURLINFO_EFFECTIVE_URL, &effective);
            if (effective) result.finalUrl = effective;
            curl_off_t downloaded = 0, uploaded = 0;
            curl_easy_getinfo(handle.get(), CURLINFO_SIZE_DOWNLOAD_T, &downloaded);
            curl_easy_getinfo(handle.get(), CURLINFO_SIZE_UPLOAD_T, &uploaded);
            result.downloadedBytes += static_cast<std::size_t>(downloaded);
            result.uploadedBytes += static_cast<std::size_t>(uploaded);
            if (code != CURLE_OK)
            {
                result.error = Error(code);
                if (capture.failure)
                {
                    result.error->kind = *capture.failure;
                    result.error->message = *capture.failure == TransportErrorKind::ResponseTooLarge ?
                        "Response body exceeded the capture limit." : *capture.failure == TransportErrorKind::HeadersTooLarge ?
                        "Response headers exceeded the capture limit." : "Unable to capture the response.";
                }
                break;
            }
            if (result.status != 301 && result.status != 302 && result.status != 303 &&
                result.status != 307 && result.status != 308) break;
            char* location = nullptr;
            curl_easy_getinfo(handle.get(), CURLINFO_REDIRECT_URL, &location);
            if (!location) break;
            const std::string next(location);
            if (Origin(next) != origin)
            {
                result.error = TransportError{ TransportErrorKind::RedirectBlocked, 0,
                    "Cross-origin or non-HTTP(S) redirect blocked; request the target explicitly." };
                break;
            }
            if (result.redirects >= options.maxRedirects)
            {
                result.error = TransportError{ TransportErrorKind::RedirectLimit, 0, "HTTP redirect limit exceeded." };
                break;
            }
            if (((result.status == 301 || result.status == 302) && method == "POST") ||
                (result.status == 303 && method != "HEAD"))
            {
                method = "GET";
                body.reset();
                redirectedToGet = true;
            }
            url = next;
            ++result.redirects;
        }
    }
    catch (const CurlFailure& failure) { result.error = Error(failure.code); }
    catch (const std::bad_alloc&)
    {
        result.error = TransportError{ TransportErrorKind::Other, 0, "Insufficient memory for HTTP execution." };
    }
    return finish();
}
}
