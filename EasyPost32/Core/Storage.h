#pragma once
#include "Collection.h"
#include "../Http/HttpClient.h"
#include <cstdint>
#include <filesystem>
#include <memory>

namespace easypost
{
std::string ReadTextFile(const std::filesystem::path& path, size_t maxBytes = 32 * 1024 * 1024);
void WriteTextAtomic(const std::filesystem::path& path, const std::string& text);
Collection LoadCollection(const std::filesystem::path& path);
void SaveCollection(const std::filesystem::path& path, const Collection& collection);

struct HistoryDetails
{
    RequestDraft request; // Resolved Send snapshot, independent of future collection edits.
    HttpResponse response;
};
struct HistoryEntry
{
    std::string timestamp, method, url;
    long status = 0;
    long long elapsedMs = 0;
    bool transportError = false;
    // Session-only fields. HistoryJson deliberately writes only the six metadata fields above.
    std::shared_ptr<const HistoryDetails> details;
    std::uint64_t id = 0;
};
std::string UtcTimestamp();
// Omit user info, path, query and fragment. Secret-bearing collections omit
// the entire endpoint, since even a hostname may be a secret substitution.
std::string HistoryUrl(const std::string& resolvedUrl, const std::vector<Variable>& variables);
class HistoryStore
{
public:
    explicit HistoryStore(std::filesystem::path file = {}): m_file(std::move(file)) {}
    bool enabled = false; // Session-only opt-in; never loaded from disk.
    void Load();
    // An explicit history Resend records that one result even if automatic recording is off.
    void Record(const HistoryEntry& entry, bool explicitResend = false);
    void Clear();
    const std::vector<HistoryEntry>& Entries() const { return m_entries; }
    std::uint64_t Revision() const { return m_revision; }
    static constexpr size_t DetailsBudget = 64 * 1024 * 1024;
private:
    std::filesystem::path m_file;
    std::vector<HistoryEntry> m_entries;
    std::uint64_t m_nextId = 1, m_revision = 0;
};
}
