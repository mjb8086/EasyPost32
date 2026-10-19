#include "HttpJobs.h"
#include <algorithm>
#include <cassert>

namespace easypost
{
const HttpResponse& HttpJob::Response() const
{
    assert(Done());
    return m_response;
}

HttpJobs::~HttpJobs() { Shutdown(); }

std::shared_ptr<HttpJob> HttpJobs::Start(HttpRequest request, HttpOptions options)
{
    Reap();
    auto job = std::make_shared<HttpJob>();
    job->m_response.tlsVerificationEnabled = options.verifyTls;
    job->m_response.error = TransportError{ TransportErrorKind::Other, 0,
        "Unable to complete HTTP execution." };
    options.cancellation = job->m_stop.get_token();
    auto worker = std::make_unique<Worker>();
    worker->job = job;
    // Register before starting: allocation failure cannot leave an untracked thread.
    m_workers.push_back(std::move(worker));
    try
    {
        m_workers.back()->thread = std::jthread(
            [job, request = std::move(request), options = std::move(options)]
            {
                try { job->m_response = Execute(request, options); }
                catch (...)
                {
                    // The preallocated fallback avoids allocation in this handler.
                }
                job->m_done.store(true, std::memory_order_release);
            });
    }
    catch (...) { m_workers.pop_back(); throw; }
    return job;
}

void HttpJobs::Reap()
{
    std::erase_if(m_workers, [](const auto& worker) { return worker->job->Done(); });
}

void HttpJobs::Shutdown()
{
    for (auto& worker : m_workers) worker->job->Cancel();
    m_workers.clear(); // jthread joins while jobs and curl are still alive.
}
}
