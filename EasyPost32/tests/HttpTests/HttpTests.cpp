#include "../../Http/HttpClient.h"
#include "../../Http/HttpJobs.h"
#include "../../RequestComposer.h"
#include "CollectionTests.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace easypost;
using nlohmann::json;

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void Success(const HttpResponse& response)
{
    if (response.error)
        throw std::runtime_error("Unexpected transport error " + std::to_string(response.error->nativeCode) + ": " + response.error->message);
    Require(response.status == 200, "Expected HTTP 200");
}
void Failure(const HttpResponse& response, TransportErrorKind kind)
{
    Require(response.error && response.error->kind == kind, "Wrong or missing transport error category");
    Require(!response.error->message.empty(), "Missing transport diagnostic");
}
std::string Hex(const std::string& bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char byte : bytes)
    {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
bool Loopback(std::string_view address, std::string_view prefix)
{
    if (!address.starts_with(prefix)) return false;
    const auto port = address.substr(prefix.size());
    return !port.empty() && std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; });
}
}

int main(int argc, char** argv)
{
    if (argc != 7 || !Loopback(argv[1], "http://127.0.0.1:") ||
        !Loopback(argv[2], "https://127.0.0.1:") || !Loopback(argv[3], "https://127.0.0.1:") ||
        !Loopback(argv[5], "http://127.0.0.1:") || !Loopback(argv[6], "http://127.0.0.1:"))
    {
        std::cerr << "Run through tests/run_http_tests.py; only loopback fixtures are accepted.\n";
        return 2;
    }
    const std::string base = argv[1], tls = argv[2], wrongTls = argv[3], refused = argv[5], truncated = argv[6];
    const std::string raw = "{\r\n\t\"message\" : \"caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x9a\x80\",\r\n  \"n\": [ 1,  2 ]\r\n}\r\n\r\n  ";
    int passed = 0, failed = 0;
    auto test = [&](const std::string& name, auto action)
    {
        try { action(); ++passed; std::cout << "PASS: " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL: " << name << ": " << error.what() << '\n'; }
    };

    test("GET preserves raw query, ordered duplicate request/response headers and metadata", [&]
    {
        HttpRequest request;
        request.url = base + "/inspect?q=a%20b&n=1&n=2";
        request.headers = { {"X-Repeat", "one"}, {"X-Repeat", "two"}, {"X-Empty", ""} };
        const auto response = Execute(request);
        Success(response);
        const auto data = json::parse(response.body);
        Require(data["method"] == "GET" && data["path"] == "/inspect" && data["query"] == "q=a%20b&n=1&n=2", "URL/method changed");
        std::vector<std::string> values;
        bool emptyHeader = false;
        for (const auto& header : data["headers"])
        {
            if (header[0] == "x-repeat") values.push_back(header[1].get<std::string>());
            if (header[0] == "x-empty" && header[1] == "") emptyHeader = true;
        }
        Require(values == std::vector<std::string>{ "one", "two" } && emptyHeader, "Request header order/repeats/empty value lost");
        values.clear();
        for (const auto& [name, value] : response.headers)
            if (name == "x-reply") values.push_back(value);
        Require(values == std::vector<std::string>{ "one", "two" }, "Repeated response headers lost");
        Require(!response.statusText.empty() && response.rawHeaders.starts_with("HTTP/"), "Response metadata missing");
        Require(response.finalUrl == request.url && response.downloadedBytes == response.body.size() && response.elapsed.count() >= 0, "Transfer metadata incorrect");
        Require(response.bodyIsText && response.tlsVerificationEnabled, "Response classification/TLS default incorrect");
    });
    for (const auto* method : { "POST", "PUT", "PATCH", "DELETE", "OPTIONS" })
        test(std::string(method) + " transmits exact raw UTF-8 body", [&]
        {
            HttpRequest request;
            request.method = method;
            request.url = base + "/inspect";
            request.headers = { {"Content-Type", "application/json"} };
            request.body = raw;
            const auto response = Execute(request);
            Success(response);
            const auto data = json::parse(response.body);
            Require(data["method"] == method && data["body_hex"] == Hex(raw), "Method/body bytes changed");
            Require(response.uploadedBytes == raw.size(), "Uploaded byte count incorrect");
        });
    test("POST preserves embedded NUL and non-UTF-8 bytes", [&]
    {
        HttpRequest request;
        request.method = "POST";
        request.url = base + "/echo";
        request.body = std::string("a\0b\xff", 4);
        const auto response = Execute(request);
        Success(response);
        Require(response.body == request.body && !response.bodyIsText, "Binary request/echo bytes changed");
    });
    test("Empty POST does not read from stdin", [&]
    {
        HttpRequest request;
        request.method = "POST";
        request.url = base + "/echo";
        const auto response = Execute(request);
        Success(response);
        Require(response.body.empty(), "Empty POST acquired a body");
    });
    test("HEAD returns metadata without capturing a response body", [&]
    {
        HttpRequest request;
        request.method = "HEAD";
        request.url = base + "/inspect";
        const auto response = Execute(request);
        Success(response);
        Require(response.body.empty() && !response.headers.empty(), "HEAD semantics incorrect");
    });
    for (const auto status : { 400, 404, 500 })
        test("HTTP " + std::to_string(status) + " remains a normal response", [&]
        {
            HttpRequest request;
            request.url = base + "/status/" + std::to_string(status);
            const auto response = Execute(request);
            Require(!response.error && response.status == status, "HTTP error became a transport failure");
            Require(response.body == "fixture status " + std::to_string(status), "HTTP error body missing");
        });
    test("Connection refusal reports a useful transport error", [&]
    {
        HttpRequest request;
        request.url = refused;
        const auto response = Execute(request);
        Failure(response, TransportErrorKind::Connection);
        Require(response.status == 0 && response.error->nativeCode != 0, "Connection failure metadata incorrect");
    });
    test("Slow response times out within the total time budget", [&]
    {
        HttpRequest request;
        request.url = base + "/delay/1500";
        HttpOptions options;
        options.totalTimeout = std::chrono::milliseconds(100);
        const auto response = Execute(request, options);
        Failure(response, TransportErrorKind::Timeout);
        Require(response.elapsed < std::chrono::seconds(3), "Timeout did not bound request duration");
    });
    test("Malformed URL and non-HTTP protocols are rejected", [&]
    {
        for (const auto* address : { "://bad", "http://", "file:///not-a-real-file", "ftp://127.0.0.1/x" })
        {
            HttpRequest request;
            request.url = address;
            Failure(Execute(request), TransportErrorKind::InvalidRequest);
        }
    });
    test("Header/method injection, embedded URL NUL and invalid options are rejected", [&]
    {
        HttpRequest request;
        request.url = base + "/inspect";
        request.headers = { {"X-Test", "value\r\nInjected: yes"} };
        Failure(Execute(request), TransportErrorKind::InvalidRequest);
        request.headers = { {"Bad:Name", "value"} };
        Failure(Execute(request), TransportErrorKind::InvalidRequest);
        request.headers.clear();
        request.method = "GET\r\n";
        Failure(Execute(request), TransportErrorKind::InvalidRequest);
        request.method = "GET";
        request.url += std::string("\0suffix", 7);
        Failure(Execute(request), TransportErrorKind::InvalidRequest);
        request.url = base;
        HttpOptions options;
        options.totalTimeout = std::chrono::milliseconds(0);
        Failure(Execute(request, options), TransportErrorKind::InvalidRequest);
    });
    test("Same-origin 307 redirects retain method/body and final URL", [&]
    {
        HttpRequest request;
        request.method = "POST";
        request.url = base + "/redirect/2";
        request.body = raw;
        const auto response = Execute(request);
        Success(response);
        const auto data = json::parse(response.body);
        Require(response.redirects == 2 && response.finalUrl == base + "/inspect", "Redirect metadata incorrect");
        Require(data["method"] == "POST" && data["body_hex"] == Hex(raw), "307 rewrote method/body");
    });
    test("303 redirect switches POST to GET without a body", [&]
    {
        HttpRequest request;
        request.method = "POST";
        request.url = base + "/redirect303";
        request.body = raw;
        request.headers = { {"Content-Length", std::to_string(raw.size())}, {"Content-Type", "application/json"} };
        const auto response = Execute(request);
        Success(response);
        const auto data = json::parse(response.body);
        Require(data["method"] == "GET" && data["body_hex"] == "", "303 semantics incorrect");
        for (const auto& header : data["headers"])
            Require(header[0] != "content-length" && header[0] != "content-type", "Redirect retained stale payload headers");
    });
    test("Redirect loops stop at the configured finite maximum", [&]
    {
        HttpRequest request;
        request.url = base + "/redirect-loop";
        HttpOptions options;
        options.maxRedirects = 2;
        const auto response = Execute(request, options);
        Failure(response, TransportErrorKind::RedirectLimit);
        Require(response.redirects == 2, "Redirect limit exceeded");
    });
    test("Cross-origin and file redirects are blocked before forwarding secrets", [&]
    {
        for (const auto& path : { "/redirect-cross?port=" + refused.substr(refused.rfind(':') + 1), std::string("/redirect-file") })
        {
            HttpRequest request;
            request.url = base + path;
            request.headers = { {"X-Api-Key", "synthetic-only"} };
            Failure(Execute(request), TransportErrorKind::RedirectBlocked);
        }
    });
    test("Streaming response capture respects the byte limit", [&]
    {
        HttpRequest request;
        request.url = base + "/large/131072";
        HttpOptions options;
        options.maxResponseBytes = 4096;
        const auto response = Execute(request, options);
        Failure(response, TransportErrorKind::ResponseTooLarge);
        Require(response.body.size() <= options.maxResponseBytes, "Body capture exceeded limit");
    });
    test("Oversized response headers are bounded", [&]
    {
        HttpRequest request;
        request.url = base + "/headers/4096";
        HttpOptions options;
        options.maxHeaderBytes = 512;
        const auto response = Execute(request, options);
        Failure(response, TransportErrorKind::HeadersTooLarge);
        Require(response.rawHeaders.size() <= options.maxHeaderBytes, "Header capture exceeded limit");
    });
    test("Binary and invalid UTF-8 responses are classified safely", [&]
    {
        for (const auto* path : { "/binary", "/invalid-utf8" })
        {
            HttpRequest request;
            request.url = base + path;
            const auto response = Execute(request);
            Success(response);
            Require(!response.bodyIsText && !response.body.empty(), "Unsafe bytes classified as text");
        }
        Require(IsTextBody(raw) && IsTextBody("\n\t\r") && !IsTextBody("\xed\xa0\x80") && !IsTextBody("\xf4\x90\x80\x80"), "UTF-8 classification incorrect");
    });
    test("Truncated response is incomplete, never a fabricated success", [&]
    {
        HttpRequest request;
        request.url = truncated;
        const auto response = Execute(request);
        Require(response.error.has_value() && response.status == 200 && response.body == "short", "Truncated body not flagged");
    });
    test("Untrusted local TLS certificate fails with verification enabled by default", [&]
    {
        HttpRequest request;
        request.url = tls + "/ready";
        const auto response = Execute(request);
        Failure(response, TransportErrorKind::Tls);
        Require(response.tlsVerificationEnabled, "TLS verification default was disabled");
    });
    test("Explicit local CA permits a valid TLS connection without changing trust stores", [&]
    {
        HttpRequest request;
        request.url = tls + "/ready";
        HttpOptions options;
        options.caFile = argv[4];
        const auto response = Execute(request, options);
        Success(response);
        Require(response.body == "ready" && response.tlsVerificationEnabled, "Verified TLS response incorrect");
    });
    test("Trusted TLS certificate with wrong hostname is rejected", [&]
    {
        HttpRequest request;
        request.url = wrongTls + "/ready";
        HttpOptions options;
        options.caFile = argv[4];
        Failure(Execute(request, options), TransportErrorKind::Tls);
    });
    test("TLS verification can only be disabled by an explicit option", [&]
    {
        HttpRequest request;
        request.url = wrongTls + "/ready";
        HttpOptions options;
        options.verifyTls = false;
        const auto response = Execute(request, options);
        Success(response);
        Require(!response.tlsVerificationEnabled && response.body == "ready", "Explicit TLS setting not reported");
    });
    test("Parallel requests own separate easy handles", [&]
    {
        std::vector<std::future<HttpResponse>> futures;
        for (int i = 0; i < 8; ++i)
            futures.push_back(std::async(std::launch::async, [&, i]
            {
                HttpRequest request;
                request.method = "POST";
                request.url = base + "/echo";
                request.body = std::to_string(i);
                return Execute(request);
            }));
        for (std::size_t i = 0; i < futures.size(); ++i)
        {
            const auto response = futures[i].get();
            Success(response);
            Require(response.body == std::to_string(i), "Concurrent request state leaked");
        }
    });
    test("Editor composition preserves body bytes and repeated header order", [&]
    {
        RequestDraft draft;
        draft.method = "PATCH";
        draft.url = base + "/inspect?literal=%2F+a";
        draft.headersText = "X-Repeat: one\r\nX-Repeat: two\r\n\r\nX-Empty:\r\n";
        draft.body = "{\r\n\t\"raw\" : [ 1,  2 ]\r\n}\r\n  ";
        HttpRequest request;
        std::string error;
        Require(ComposeRequest(draft, request, error), "Valid editor headers rejected");
        Require(request.body == draft.body && request.headers.size() == 3 &&
            request.headers[1] == Header{"X-Repeat", "two"}, "Editor draft changed");
        const auto response = Execute(request);
        Success(response);
        Require(json::parse(response.body)["body_hex"] == Hex(draft.body), "Composed body rewritten");
    });
    test("Incomplete editor headers fail with a safe line-number diagnostic", [&]
    {
        RequestDraft draft;
        draft.headersText = "X-First: ok\r\nsynthetic-secret-without-colon";
        HttpRequest request;
        std::string error;
        Require(!ComposeRequest(draft, request, error) && error.find("line 2") != error.npos &&
            error.find("synthetic-secret") == error.npos, "Header diagnostic unsafe or unclear");
    });
    test("Pre-cancelled request performs no transfer", [&]
    {
        std::stop_source stop;
        stop.request_stop();
        HttpOptions options;
        options.cancellation = stop.get_token();
        HttpRequest request;
        request.url = base + "/ready";
        const auto response = Execute(request, options);
        Failure(response, TransportErrorKind::Cancelled);
        Require(response.status == 0 && response.downloadedBytes == 0, "Cancelled request transferred data");
    });
    test("Worker owns a snapshot and publishes a complete response", [&]
    {
        HttpJobs jobs;
        HttpRequest request;
        request.method = "POST";
        request.url = base + "/echo";
        request.body = "snapshot";
        auto job = jobs.Start(request, {});
        request.body = "edited afterwards";
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!job->Done() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(job->Done(), "Worker failed to complete");
        jobs.Reap();
        Success(job->Response());
        Require(job->Response().body == "snapshot", "Worker referenced a mutable draft");
    });
    test("Cancellation interrupts a pending worker", [&]
    {
        HttpJobs jobs;
        HttpRequest request;
        request.url = base + "/delay/3000";
        auto job = jobs.Start(request, {});
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        job->Cancel();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!job->Done() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(job->Done(), "Cancellation did not interrupt the transfer");
        Failure(job->Response(), TransportErrorKind::Cancelled);
    });
    test("Dropping a document's job reference does not destroy a running worker", [&]
    {
        HttpJobs jobs;
        HttpRequest request;
        request.url = base + "/delay/3000";
        auto job = jobs.Start(request, {});
        std::weak_ptr<HttpJob> observer = job;
        job->Cancel();
        job.reset();
        jobs.Shutdown();
        Require(observer.expired(), "Shutdown retained the worker job");
    });
    RunCollectionTests(base, test);
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
