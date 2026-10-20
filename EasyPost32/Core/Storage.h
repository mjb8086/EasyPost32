#pragma once
#include "Collection.h"
#include <filesystem>

namespace easypost
{
std::string ReadTextFile(const std::filesystem::path& path, size_t maxBytes = 32 * 1024 * 1024);
void WriteTextAtomic(const std::filesystem::path& path, const std::string& text);
Collection LoadCollection(const std::filesystem::path& path);
void SaveCollection(const std::filesystem::path& path, const Collection& collection);

struct HistoryEntry
{
    std::string timestamp, method, url;
    long status = 0;
    long long elapsedMs = 0;
    bool transportError = false;
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
    void Record(const HistoryEntry& entry);
    void Clear();
    const std::vector<HistoryEntry>& Entries() const { return m_entries; }
private:
    std::filesystem::path m_file;
    std::vector<HistoryEntry> m_entries;
};
}
