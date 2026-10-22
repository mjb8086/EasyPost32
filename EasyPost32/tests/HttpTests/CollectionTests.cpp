#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "CollectionTests.h"
#include "../../Core/Collection.h"
#include "../../Core/Storage.h"
#include "../../RequestComposer.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace easypost;
using nlohmann::json;
namespace
{
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<typename F> void Reject(F action)
{
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid input unexpectedly accepted");
}
struct Temporary
{
    std::filesystem::path root = std::filesystem::absolute("build");
    std::filesystem::path path = root / ("core-tests-" + NewId());
    Temporary() { std::filesystem::create_directories(root); Require(std::filesystem::create_directory(path), "Test directory collision"); }
    ~Temporary()
    {
        // These are freshly created private fixtures, never a supplied path.
        if (path.is_absolute() && path.parent_path() == root && path.filename().wstring().starts_with(L"core-tests-"))
        { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    }
};
Collection Sample()
{
    auto value = NewCollection("Synthetic caf\xc3\xa9 collection");
    value.variables = { {"baseUrl", "http://127.0.0.1:8080"}, {"message", "caf\xc3\xa9"} };
    value.items[0].request->method = "POST";
    value.items[0].request->url = "{{baseUrl}}/inspect?token=synthetic-query";
    value.items[0].request->headersText = "X-Repeat: one\r\nX-Repeat: two\r\nUnfinished header";
    value.items[0].request->body = "{\r\n\t\"message\" : \"{{message}}\"\r\n}\r\n  ";
    value.items[0].request->verifyTls = false;
    CollectionItem folder{ NewId(), "Outer", {}, {} };
    CollectionItem nested{ NewId(), "Inner", {}, {} };
    nested.items.push_back(value.items.back());
    value.items.pop_back();
    folder.items.push_back(std::move(nested));
    value.items.push_back(std::move(folder));
    return value;
}
}
void RunCollectionTests(const std::string& base, const std::function<void(const char*, std::function<void()>)>& test)
{
    test("Native collection round trip preserves nested order, UTF-8, raw source and TLS", []
    {
        const auto source = Sample();
        Require(DecodeCollection(EncodeCollection(source)) == source, "Native round trip lost supported fields");
    });
    test("Atomic save and reopen preserve an existing collection", []
    {
        Temporary temporary;
        const auto path = temporary.path / L"caf\x00e9.dp32.json";
        auto source = Sample();
        SaveCollection(path, source);
        source.name = "Replacement";
        SaveCollection(path, source);
        Require(LoadCollection(path) == source, "Atomic replacement lost collection data");
        Require(std::distance(std::filesystem::directory_iterator(temporary.path), std::filesystem::directory_iterator{}) == 1,
            "Temporary save file was left behind");
    });
    test("Failed atomic replacement retains the complete original file", []
    {
        Temporary temporary;
        const auto path = temporary.path / "locked.dp32.json";
        const auto source = Sample();
        SaveCollection(path, source);
        HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Require(lock != INVALID_HANDLE_VALUE, "Cannot lock fixture");
        try
        {
            auto changed = source;
            changed.name = "Must not replace";
            Reject([&] { SaveCollection(path, changed); });
        }
        catch (...) { CloseHandle(lock); throw; }
        CloseHandle(lock);
        Require(LoadCollection(path) == source, "Failed save damaged the original");
    });
    test("Secret variable values are omitted and require re-entry after reopen", []
    {
        auto source = Sample();
        source.variables.push_back({"token", "synthetic-secret-never-save", true});
        const auto encoded = EncodeCollection(source);
        Require(encoded.find("synthetic-secret-never-save") == encoded.npos, "Secret value persisted");
        const auto loaded = DecodeCollection(encoded);
        Require(loaded.variables.back().secret && loaded.variables.back().value.empty(), "Secret marker lost");
        RequestDraft draft;
        draft.url = "http://127.0.0.1/{{token}}";
        Reject([&] { (void)ResolveDraft(draft, loaded.variables); });
    });
    test("Malformed JSON, versions and field types fail without mutating the model", []
    {
        const auto source = Sample();
        Reject([] { (void)DecodeCollection("{"); });
        auto value = json::parse(EncodeCollection(source));
        value["version"] = 2;
        Reject([&] { (void)DecodeCollection(value.dump()); });
        value["version"] = "1";
        Reject([&] { (void)DecodeCollection(value.dump()); });
        Require(source.name == "Synthetic caf\xc3\xa9 collection", "Rejected load mutated existing content");
    });
    test("Duplicate JSON keys, identifiers and unknown native fields are rejected", []
    {
        Reject([] { (void)DecodeCollection("{\"version\":1,\"version\":2}"); });
        auto value = json::parse(EncodeCollection(Sample()));
        value["items"][1]["id"] = value["items"][0]["id"];
        Reject([&] { (void)DecodeCollection(value.dump()); });
        value = json::parse(EncodeCollection(Sample()));
        value["futureField"] = true;
        Reject([&] { (void)DecodeCollection(value.dump()); });
    });
    test("Plaintext values on marked secret fields are rejected on load", []
    {
        auto value = json::parse(EncodeCollection(Sample()));
        value["variables"].push_back({{"name", "token"}, {"secret", true}, {"value", "must-not-load"}});
        Reject([&] { (void)DecodeCollection(value.dump()); });
    });
    test("Oversized files, excessive folder depth and embedded NUL text are rejected", []
    {
        Reject([] { (void)DecodeCollection(std::string(32 * 1024 * 1024 + 1, ' ')); });
        auto value = json::parse(EncodeCollection(Sample()));
        value["name"] = std::string("a\0b", 3);
        Reject([&] { (void)DecodeCollection(value.dump()); });
        auto source = Sample();
        for (int depth = 0; depth < 34; ++depth)
        {
            CollectionItem folder{NewId(), "Nested", {}, std::move(source.items)};
            source.items = {std::move(folder)};
        }
        Reject([&] { (void)EncodeCollection(source); });
    });
    test("Variable substitution is send-only and does not URL-encode or format bytes", []
    {
        auto source = Sample();
        auto draft = *source.items[0].request;
        draft.headersText = "X-Message: {{message}}\r\nX-Repeat: one\r\nX-Repeat: two";
        draft.url += "&literal=%2F+a";
        const auto resolved = ResolveDraft(draft, source.variables);
        Require(resolved.url.ends_with("&literal=%2F+a") && resolved.body == "{\r\n\t\"message\" : \"caf\xc3\xa9\"\r\n}\r\n  ",
            "Expansion formatted or encoded source");
        Require(draft.body.find("{{message}}") != draft.body.npos && draft.url.starts_with("{{baseUrl}}"), "Source was mutated");
    });
    test("Resolved variable request reaches the real loopback HTTP engine", [&]
    {
        RequestDraft draft;
        draft.method = "POST";
        draft.url = "{{baseUrl}}/inspect?raw=%2F+a";
        draft.headersText = "X-Token: {{token}}";
        draft.body = "  {{payload}}\r\n";
        const auto resolved = ResolveDraft(draft, {{"baseUrl", base}, {"token", "synthetic-session-secret", true}, {"payload", "{ \"a\" : 1 }"}});
        HttpRequest request;
        std::string error;
        Require(ComposeRequest(resolved, request, error), "Composition failed");
        const auto response = Execute(request);
        Require(!response.error && response.status == 200, "Resolved HTTP request failed");
        const auto body = json::parse(response.body);
        Require(body["query"] == "raw=%2F+a" && body["body_hex"] == "20207b20226122203a2031207d0d0a", "Resolved wire bytes differ");
    });
    test("Unresolved, duplicate, malformed and recursive variables fail clearly", []
    {
        RequestDraft draft;
        draft.url = "{{missing}}/ready";
        Reject([&] { (void)ResolveDraft(draft, {}); });
        Reject([&] { (void)ResolveDraft(draft, {{"a", "one"}, {"a", "two"}}); });
        draft.url = "{{a}}";
        Reject([&] { (void)ResolveDraft(draft, {{"a", "{{b}}"}, {"b", "value"}}); });
        draft.url = "{{unclosed";
        Reject([&] { (void)ResolveDraft(draft, {}); });
        draft.url = "stray}}";
        Reject([&] { (void)ResolveDraft(draft, {}); });
    });
    test("URL and header variable values cannot inject new lines", []
    {
        RequestDraft draft;
        draft.url = "http://127.0.0.1/ready";
        draft.headersText = "X-Value: {{value}}";
        Reject([&] { (void)ResolveDraft(draft, {{"value", "ok\r\nAuthorization: synthetic"}}); });
        draft.headersText.clear();
        draft.body = "{{value}}";
        Require(ResolveDraft(draft, {{"value", "body\r\nallowed"}}).body == "body\r\nallowed", "Body expansion lost line breaks");
    });
    test("Folder duplication renews nested IDs and preserves original ordering", []
    {
        const auto source = Sample();
        auto copy = source.items[1];
        RenewIds(copy);
        Require(copy.id != source.items[1].id && copy.items[0].items[0].id != source.items[1].items[0].items[0].id &&
            copy.items[0].items[0].request == source.items[1].items[0].items[0].request, "Duplicate shared IDs or lost data");
    });
    test("History URL omits user info, paths, queries, fragments and secret endpoints", []
    {
        Require(HistoryUrl("https://user:synthetic-password@example.invalid:8443/secret/token?api_key=synthetic#fragment", {}) ==
            "https://example.invalid:8443", "History retained sensitive URL components");
        Require(HistoryUrl("https://secret-host.invalid/", {{"private", "secret-host.invalid", true}}) == "[endpoint omitted]",
            "History retained a secret endpoint");
    });
    test("History is opt-in and stores only metadata fields", []
    {
        Temporary temporary;
        const auto path = temporary.path / "history.json";
        HistoryStore store(path);
        HistoryEntry entry{ UtcTimestamp(), "POST", HistoryUrl("http://127.0.0.1/hidden?token=synthetic", {}), 200, 12, false };
        store.Record(entry);
        Require(!std::filesystem::exists(path), "Disabled history wrote a file");
        store.enabled = true;
        store.Record(entry);
        const auto text = ReadTextFile(path);
        Require(text.find("hidden") == text.npos && text.find("synthetic") == text.npos && text.find("body") == text.npos &&
            json::parse(text)["entries"][0].size() == 6, "History included non-metadata content");
        HistoryStore loaded(path);
        loaded.Load();
        Require(!loaded.enabled && loaded.Entries().size() == 1, "History enabled itself or lost entries");
        loaded.Clear();
        HistoryStore cleared(path);
        cleared.Load();
        Require(cleared.Entries().empty(), "Cleared history remained on disk");
    });
    test("Malformed history is rejected without replacing visible entries", []
    {
        Temporary temporary;
        const auto path = temporary.path / "history.json";
        HistoryStore store(path);
        store.enabled = true;
        store.Record({UtcTimestamp(), "GET", "http://127.0.0.1", 200, 1});
        WriteTextAtomic(path, "{\"format\":\"unknown\"}");
        Reject([&] { store.Load(); });
        Require(store.Entries().size() == 1, "Failed history load changed existing entries");
        store.Clear();
    });
    test("History sanitizes caller URLs again and bounds retained entries to 500", []
    {
        Temporary temporary;
        const auto path = temporary.path / "history.json";
        HistoryStore store(path);
        store.enabled = true;
        store.Record({UtcTimestamp(), "GET", "https://name:password@example.invalid/private?secret=value", 200, 0});
        auto root = json::parse(ReadTextFile(path));
        Require(root["entries"][0]["url"] == "https://example.invalid", "History trusted an unsanitized caller URL");
        auto entry = root["entries"][0];
        root["entries"] = json::array();
        for (int index = 0; index < 500; ++index)
        {
            entry["elapsedMs"] = index;
            root["entries"].push_back(entry);
        }
        WriteTextAtomic(path, root.dump());
        store.Load();
        store.Record({UtcTimestamp(), "POST", "https://example.invalid", 201, 500});
        HistoryStore loaded(path);
        loaded.Load();
        Require(loaded.Entries().size() == 500 && loaded.Entries().front().elapsedMs == 1 &&
            loaded.Entries().back().elapsedMs == 500, "History retention did not discard the oldest entry");
    });
    test("History details retain the sent snapshot but never reach the metadata file", []
    {
        Temporary temporary;
        const auto path = temporary.path / "history.json";
        HistoryStore store(path);
        store.enabled = true;
        RequestDraft draft{"Synthetic send"};
        draft.method = "POST";
        draft.url = "https://example.invalid/path?token=SYNTHETIC_QUERY";
        draft.headersText = "Authorization: Bearer SYNTHETIC_HEADER\r\nX-Repeat: one\r\nX-Repeat: two";
        draft.body = "{\r\n \"body\" : \"SYNTHETIC_REQUEST\"\r\n}\r\n";
        draft.verifyTls = false;
        HttpResponse response;
        response.status = 200;
        response.rawHeaders = "Set-Cookie: SYNTHETIC_COOKIE\r\n";
        response.body = "SYNTHETIC_RESPONSE";
        HistoryEntry entry{UtcTimestamp(), "POST", HistoryUrl(draft.url, {}), 200, 5};
        entry.details = std::make_shared<const HistoryDetails>(HistoryDetails{draft, response});
        store.Record(entry);
        draft.body = "Changed after Send";
        Require(store.Entries()[0].details->request.body.find("SYNTHETIC_REQUEST") != std::string::npos &&
            !store.Entries()[0].details->request.verifyTls && store.Entries()[0].details->response.body == response.body,
            "History lost its original request, response or TLS snapshot");
        const auto text = ReadTextFile(path);
        Require(text.find("SYNTHETIC_") == text.npos && json::parse(text)["entries"][0].size() == 6,
            "Detailed history leaked into persistent metadata");
        HistoryStore reopened(path); reopened.Load();
        Require(reopened.Entries().size() == 1 && !reopened.Entries()[0].details && reopened.Entries()[0].id != 0 &&
            !reopened.enabled, "Reopened metadata pretended to have request/response details");
    });
    test("Explicit history resend appends a new result without enabling automatic recording", []
    {
        Temporary temporary;
        HistoryStore store(temporary.path / "history.json");
        HistoryEntry original{UtcTimestamp(), "GET", "http://127.0.0.1", 200, 1};
        original.details = std::make_shared<const HistoryDetails>(HistoryDetails{RequestDraft{"Original"}, HttpResponse{}});
        store.Record(original);
        Require(store.Entries().empty(), "Default recording was enabled");
        store.Record(original, true);
        const auto id = store.Entries()[0].id;
        const auto revision = store.Revision();
        auto replay = original; replay.status = 500; replay.elapsedMs = 2;
        store.Record(replay, true);
        Require(!store.enabled && store.Entries().size() == 2 && store.Entries()[0].id == id &&
            store.Entries()[1].id > id && store.Entries()[0].status == 200 && store.Entries()[1].status == 500 &&
            store.Revision() > revision, "Resend overwrote the original, reused an ID or enabled automatic recording");
        store.Clear();
        Require(store.Entries().empty(), "Clear retained replay rows");
    });
    test("History releases the oldest detailed snapshots at the memory budget while keeping metadata", []
    {
        Temporary temporary;
        HistoryStore store(temporary.path / "history.json");
        store.enabled = true;
        std::weak_ptr<const HistoryDetails> oldest, newest;
        for (int index = 0; index < 2; ++index)
        {
            HistoryDetails details;
            details.request.body.assign(HistoryStore::DetailsBudget / 2 + 1, 'x');
            HistoryEntry entry{UtcTimestamp(), "POST", "http://127.0.0.1", 200, index};
            entry.details = std::make_shared<const HistoryDetails>(std::move(details));
            if (index == 0) oldest = entry.details; else newest = entry.details;
            store.Record(entry);
        }
        Require(store.Entries().size() == 2 && !store.Entries()[0].details && store.Entries()[1].details &&
            oldest.expired() && !newest.expired(), "History memory budget did not release the oldest snapshot");
        store.Clear();
        Require(newest.expired(), "Clear retained session detail buffers");
    });
    test("Failed history writes retain the original visible detail snapshot and revision", []
    {
        Temporary temporary;
        const auto path = temporary.path / "history.json";
        HistoryStore store(path); store.enabled = true;
        HistoryEntry entry{UtcTimestamp(), "GET", "http://127.0.0.1", 200, 1};
        entry.details = std::make_shared<const HistoryDetails>();
        store.Record(entry);
        const auto original = store.Entries()[0].details;
        const auto revision = store.Revision();
        const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Require(handle != INVALID_HANDLE_VALUE, "Could not lock synthetic history file");
        try { Reject([&] { store.Record(entry); }); }
        catch (...) { CloseHandle(handle); throw; }
        CloseHandle(handle);
        Require(store.Entries().size() == 1 && store.Entries()[0].details == original && store.Revision() == revision,
            "Failed save altered history snapshots");
    });
    test("Checked-in loopback collection is a valid native sample", []
    {
        const auto sample = LoadCollection(std::filesystem::path("tests") / "Fixtures" / "loopback.dp32.json");
        Require(sample.items.size() == 2 && sample.items[0].items.size() == 2 && sample.variables[0].name == "baseUrl",
            "Sample collection has unexpected structure");
        Require(DecodeCollection(EncodeCollection(sample)) == sample, "Sample did not round trip");
    });
}
