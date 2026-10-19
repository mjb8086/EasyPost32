#pragma once

#include "HttpClient.h"
#include <atomic>
#include <memory>
#include <thread>

namespace easypost
{
// A worker publishes once. Read Response() only after Done() acquires completion.
// Neither the job nor the worker knows about documents, windows or UI callbacks.
class HttpJob
{
public:
    void Cancel() noexcept { m_stop.request_stop(); }
    bool Done() const noexcept { return m_done.load(std::memory_order_acquire); }
    const HttpResponse& Response() const;

private:
    friend class HttpJobs;
    std::stop_source m_stop;
    std::atomic<bool> m_done{ false };
    HttpResponse m_response;
};

// Access the registry from its owning (UI) thread. Completed threads are reaped;
// shutdown cancels and joins every remaining thread before libcurl cleanup.
class HttpJobs
{
public:
    ~HttpJobs();
    std::shared_ptr<HttpJob> Start(HttpRequest request, HttpOptions options);
    void Reap();
    void Shutdown();

private:
    struct Worker
    {
        std::shared_ptr<HttpJob> job;
        std::jthread thread;
    };
    std::vector<std::unique_ptr<Worker>> m_workers;
};
}
