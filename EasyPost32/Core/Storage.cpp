#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "Storage.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>

namespace easypost
{
namespace
{
struct FileCloser { void operator()(void* handle) const { CloseHandle(handle); } };
using File = std::unique_ptr<void, FileCloser>;
void Require(bool success, const char* error) { if (!success) throw std::runtime_error(error); }
void ValidateHistory(const HistoryEntry& entry)
{
    static const std::set<std::string> methods{ "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS" };
    Require(methods.contains(entry.method) && entry.timestamp.size() == 20 &&
        entry.timestamp.find_first_not_of("0123456789-T:Z") == entry.timestamp.npos &&
        entry.status >= 0 && entry.status <= 999 && entry.elapsedMs >= 0 && entry.url.size() <= 520,
        "Invalid local history metadata.");
}
nlohmann::json HistoryJson(const std::vector<HistoryEntry>& entries)
{
    auto rows = nlohmann::json::array();
    for (const auto& entry : entries)
        rows.push_back({ {"timestamp", entry.timestamp}, {"method", entry.method}, {"url", entry.url},
            {"status", entry.status}, {"elapsedMs", entry.elapsedMs}, {"transportError", entry.transportError} });
    return { {"format", "EasyPost32.history"}, {"version", 1}, {"entries", rows} };
}
}
std::string ReadTextFile(const std::filesystem::path& path, size_t maxBytes)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    Require(input.good(), "Unable to open the local file.");
    const auto size = input.tellg();
    Require(size >= 0 && static_cast<unsigned long long>(size) <= maxBytes, "Local file exceeds its size limit.");
    std::string text(static_cast<size_t>(size), '\0');
    input.seekg(0);
    if (!text.empty()) input.read(text.data(), static_cast<std::streamsize>(text.size()));
    Require(input.good(), "Unable to read the complete local file.");
    return text;
}
void WriteTextAtomic(const std::filesystem::path& path, const std::string& text)
{
    const auto temporary = path.parent_path() / (L".dp32-" + std::filesystem::path(NewId()).wstring() + L".tmp");
    const auto raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(raw != INVALID_HANDLE_VALUE, "Unable to create a temporary file beside the destination.");
    File output(raw);
    try
    {
        for (size_t offset = 0; offset < text.size();)
        {
            const auto size = static_cast<DWORD>((std::min)(text.size() - offset, size_t{1024 * 1024}));
            DWORD written = 0;
            Require(WriteFile(output.get(), text.data() + offset, size, &written, nullptr) && written != 0,
                "Unable to write the complete local file.");
            offset += written;
        }
        Require(FlushFileBuffers(output.get()) != FALSE, "Unable to flush the local file.");
        output.reset();
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            Require(ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr) != FALSE,
                "Unable to replace the destination; its previous contents were retained.");
        else
            Require(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE,
                "Unable to install the new local file.");
    }
    catch (...)
    {
        output.reset();
        DeleteFileW(temporary.c_str());
        throw;
    }
}
Collection LoadCollection(const std::filesystem::path& path) { return DecodeCollection(ReadTextFile(path)); }
void SaveCollection(const std::filesystem::path& path, const Collection& collection)
{
    WriteTextAtomic(path, EncodeCollection(collection));
}
std::string UtcTimestamp()
{
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char text[32]{};
    sprintf_s(text, "%04u-%02u-%02uT%02u:%02u:%02uZ", time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond);
    return text;
}
std::string HistoryUrl(const std::string& url, const std::vector<Variable>& variables)
{
    if (std::any_of(variables.begin(), variables.end(), [](const auto& var) { return var.secret; }))
        return "[endpoint omitted]";
    const auto start = url.starts_with("https://") ? size_t{8} : url.starts_with("http://") ? size_t{7} : 0;
    if (!start || url.find_first_of("\r\n\t") != url.npos) return "[endpoint omitted]";
    auto authority = url.substr(start, url.find_first_of("/?#", start) - start);
    if (const auto user = authority.rfind('@'); user != authority.npos) authority.erase(0, user + 1);
    if (authority.empty() || authority.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.:[]") != authority.npos)
        return "[endpoint omitted]";
    return authority.size() <= 512 ? url.substr(0, start) + authority : "[endpoint omitted]";
}
void HistoryStore::Load()
{
    if (m_file.empty() || !std::filesystem::exists(m_file)) return;
    try
    {
        const auto root = nlohmann::json::parse(ReadTextFile(m_file, 512 * 1024));
        Require(root.at("format") == "EasyPost32.history" && root.at("version") == 1 &&
            root.at("entries").is_array() && root.at("entries").size() <= 500, "Unsupported local history format.");
        std::vector<HistoryEntry> entries;
        for (const auto& value : root.at("entries"))
        {
            HistoryEntry entry{ value.at("timestamp").get<std::string>(), value.at("method").get<std::string>(),
                value.at("url").get<std::string>(), value.at("status").get<long>(), value.at("elapsedMs").get<long long>(),
                value.at("transportError").get<bool>() };
            // Loaded history must meet the same conservative URL policy.
            Require(entry.url == "[endpoint omitted]" || HistoryUrl(entry.url, {}) == entry.url, "Unsafe URL in local history.");
            ValidateHistory(entry);
            entries.push_back(std::move(entry));
        }
        m_entries = std::move(entries);
    }
    catch (const nlohmann::json::exception&) { throw std::runtime_error("Invalid local history JSON."); }
}
void HistoryStore::Record(const HistoryEntry& entry)
{
    if (!enabled) return;
    auto safe = entry;
    safe.url = HistoryUrl(entry.url, {});
    ValidateHistory(safe);
    auto next = m_entries;
    next.push_back(std::move(safe));
    if (next.size() > 500) next.erase(next.begin());
    Require(!m_file.empty(), "Local history storage is unavailable.");
    std::filesystem::create_directories(m_file.parent_path());
    WriteTextAtomic(m_file, HistoryJson(next).dump(2) + "\n");
    m_entries = std::move(next);
}
void HistoryStore::Clear()
{
    // Persist an empty valid history; failure retains the visible entries.
    if (!m_file.empty() && std::filesystem::exists(m_file)) WriteTextAtomic(m_file, HistoryJson({}).dump(2) + "\n");
    m_entries.clear();
}
}
