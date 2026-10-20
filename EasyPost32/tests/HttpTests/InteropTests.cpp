#include "InteropTests.h"
#include "../../Core/Interop.h"
#include "../../Core/Storage.h"
#include "../../RequestComposer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

using namespace easypost;
using Json = nlohmann::ordered_json;
namespace
{
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void Reject(F action)
{
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid import was unexpectedly accepted");
}
std::string Fixture(const char* name) { return ReadTextFile(std::filesystem::path("tests") / "Fixtures" / name); }
bool Has(const std::vector<std::string>& notes, const char* part)
{
    return std::any_of(notes.begin(), notes.end(), [part](const auto& note) { return note.find(part) != note.npos; });
}
void Normalize(Collection& value)
{
    value.id.clear(); value.compatibilityNotes.clear();
    std::function<void(std::vector<CollectionItem>&)> visit = [&](auto& items)
    { for (auto& item : items) { item.id.clear(); visit(item.items); } };
    visit(value.items);
}
Json MinimalApi()
{
    return Json::parse(R"({"openapi":"3.1.1","info":{"title":"Synthetic","version":"1"},
        "servers":[{"url":"http://127.0.0.1:8080"}],
        "paths":{"/inspect":{"get":{"responses":{"200":{"description":"OK"}}}}}})");
}
const RequestDraft& Nested(const Collection& value) { return *value.items.at(0).items.at(0).items.at(0).request; }
void ExportForSchema(const char* name, const std::string& text)
{
    // The runner supplies a newly created private directory, and validates every
    // emitted document with the checked-in official schema before cleanup.
    char* path = nullptr; size_t size = 0;
    if (_dupenv_s(&path, &size, "EASYPOST_INTEROP_OUTPUT") != 0 || !path) throw std::runtime_error("Interop schema output directory is required");
    const std::filesystem::path directory(path); free(path);
    WriteTextAtomic(directory / name, text);
}
HttpResponse SendDraft(const RequestDraft& draft, const std::vector<Variable>& variables)
{
    HttpRequest request; std::string error;
    Require(ComposeRequest(ResolveDraft(draft, variables), request, error), "Imported request could not be composed");
    auto result = Execute(request);
    Require(!result.error && result.status == 200, "Imported loopback request failed");
    return result;
}
}
void RunInteropTests(const std::string& base, const std::function<void(const char*, std::function<void()>)>& test)
{
    test("Postman v2.1 nested fixtures preserve names, descriptions, repeated headers and exact raw JSON", []
    {
        const auto value = ImportPostman(Fixture("postman-v21.json"));
        const auto& draft = Nested(value);
        Require(value.name == "Synthetic loopback API" && value.items.size() == 2 && value.items[0].folderName == "Outer" &&
            value.items[0].items[0].folderName == "Inner" && draft.method == "POST" && draft.verifyTls &&
            draft.body == "{\r\n\t\"message\" : \"caf\xc3\xa9\"\r\n}\r\n  " && draft.headersText.starts_with("X-Repeat: one\r\nX-Repeat: two") &&
            value.variables[0].name == "baseUrl" && !value.description.empty() && !value.items[0].description.empty(), "Postman mapping lost source fields");
        Require(value.items[1].request->url == "http://127.0.0.1:8080/inspect?message=a%20b%26c&repeat=1&repeat=2", "Structured URL mapping incorrect");
        auto conflicting = Json::parse(Fixture("postman-v21.json"));
        conflicting["item"][0]["item"][0]["item"][0]["description"] = "Separate item description";
        const auto mapped = ImportPostman(conflicting.dump());
        Require(Has(mapped.compatibilityNotes, "separate item descriptions"), "Description precedence loss was not reported");
    });
    test("Postman import-export-import preserves supported fields and emits schema candidate", []
    {
        auto source = ImportPostman(Fixture("postman-v21.json"));
        const auto output = ExportPostman(source);
        ExportForSchema("nested.postman.json", output.text);
        auto result = ImportPostman(output.text);
        Normalize(source); Normalize(result);
        Require(source == result, "Supported Postman round trip lost fields");
    });
    test("Postman script/auth/body/example losses are reported and raw source is never retained", []
    {
        const auto value = ImportPostman(Fixture("postman-unsupported.json"));
        Require(value.items.size() == 1 && value.items[0].request->body.empty() && Has(value.compatibilityNotes, "scripts/tests") &&
            Has(value.compatibilityNotes, "authentication") && Has(value.compatibilityNotes, "Non-raw") &&
            Has(value.compatibilityNotes, "response examples") && Has(value.compatibilityNotes, "methods"), "Missing unsupported-feature report");
        const auto saved = EncodeCollection(value);
        Require(saved.find("NEVER_EXECUTE") == saved.npos && saved.find("SYNTHETIC_AUTH_VALUE") == saved.npos &&
            saved.find("SYNTHETIC_SAVED_RESPONSE") == saved.npos, "Unsupported secret/script material was retained");
        const auto reopened = DecodeCollection(saved);
        Require(reopened.compatibilityNotes == value.compatibilityNotes && Has(ExportPostman(reopened).notes, "scripts/tests"), "Loss warnings disappeared after native save");
    });
    test("Postman export omits marked secrets and reports TLS loss without changing source", []
    {
        auto value = ImportPostman(Fixture("postman-v21.json"));
        value.variables.push_back({"token", "SYNTHETIC_SESSION_SECRET", true});
        value.items[1].request->verifyTls = false;
        auto output = ExportPostman(value);
        Require(output.text.find("SYNTHETIC_SESSION_SECRET") == output.text.npos && Has(output.notes, "Marked secret") &&
            Has(output.notes, "TLS") && !value.items[1].request->verifyTls && value.variables.back().value == "SYNTHETIC_SESSION_SECRET", "Secret/TLS export policy failed");
        ExportForSchema("secrets-omitted.postman.json", output.text);
        Require(ImportPostman(output.text).items[1].request->verifyTls, "Imported TLS validation was disabled");
    });
    test("Postman string request and raw header text round trip without rewriting", []
    {
        auto root = Json::parse(Fixture("postman-v21.json"));
        root["item"] = Json::array({{{"name", "String request"}, {"request", "{{baseUrl}}/inspect"}}});
        auto value = ImportPostman(root.dump());
        value.items[0].request->headersText = "X-Repeat: one\nX-Repeat: two\r\nUnfinished header\r\n";
        const auto output = ExportPostman(value); ExportForSchema("raw-headers.postman.json", output.text);
        const auto result = ImportPostman(output.text);
        Require(result.items[0].request == value.items[0].request, "Raw header source was rewritten");
    });
    test("Imported secret classifications remain session-only through native save and Postman export", []
    {
        auto root = Json::parse(Fixture("postman-v21.json"));
        root["variable"].push_back({{"key", "token"}, {"value", "SYNTHETIC_IMPORTED_SECRET"}, {"type", "secret"}});
        const auto value = ImportPostman(root.dump());
        const auto native = EncodeCollection(value);
        const auto output = ExportPostman(value);
        Require(value.variables.back().secret && value.variables.back().value == "SYNTHETIC_IMPORTED_SECRET" &&
            native.find("SYNTHETIC_IMPORTED_SECRET") == native.npos && DecodeCollection(native).variables.back().value.empty() &&
            output.text.find("SYNTHETIC_IMPORTED_SECRET") == output.text.npos && Has(output.notes, "Marked secret"), "Imported secret was persisted or lost its classification");
    });
    test("Postman rejects bad versions, duplicate keys, types, duplicate variables, NUL and UTF-8", []
    {
        Reject([] { ImportPostman("{"); });
        Reject([] { ImportPostman("{\"info\":{},\"info\":{}}"); });
        auto root = Json::parse(Fixture("postman-v21.json"));
        root["info"]["schema"] = "https://example.invalid/v2.0"; Reject([&] { ImportPostman(root.dump()); });
        root = Json::parse(Fixture("postman-v21.json")); root["item"] = "bad"; Reject([&] { ImportPostman(root.dump()); });
        root = Json::parse(Fixture("postman-v21.json")); root["variable"].push_back(root["variable"][0]); Reject([&] { ImportPostman(root.dump()); });
        root = Json::parse(Fixture("postman-v21.json")); root["info"]["name"] = std::string("bad\0text", 8); Reject([&] { ImportPostman(root.dump()); });
        Reject([] { ImportPostman(std::string("{\"bad\":\"") + '\xff' + "\"}"); });
    });
    test("Postman disabled headers, variables and body are omitted explicitly", []
    {
        auto root = Json::parse(Fixture("postman-v21.json"));
        root["variable"].push_back({{"key", "off"}, {"value", "unused"}, {"disabled", true}});
        auto& request = root["item"][0]["item"][0]["item"][0]["request"];
        request["header"][0]["disabled"] = true; request["body"]["disabled"] = true;
        auto value = ImportPostman(root.dump());
        Require(value.variables.size() == 1 && Nested(value).body.empty() && Nested(value).headersText.find("one") == std::string::npos &&
            Has(value.compatibilityNotes, "Disabled") && Has(value.compatibilityNotes, "disabled Postman body"), "Disabled source material became active");
    });
    test("Postman imported request sends repeated headers, query and exact UTF-8 bytes on loopback", [&]
    {
        auto value = ImportPostman(Fixture("postman-v21.json")); value.variables[0].value = base;
        const auto& draft = Nested(value);
        auto response = SendDraft(draft, value.variables);
        const auto wire = Json::parse(response.body);
        std::string hex; static const char* digits = "0123456789abcdef";
        for (unsigned char c : draft.body) { hex += digits[c >> 4]; hex += digits[c & 15]; }
        Require(wire["method"] == "POST" && wire["query"] == "one=1&one=2" && wire["body_hex"] == hex, "Imported wire bytes differ");
        int repeats = 0; for (const auto& header : wire["headers"]) if (header[0] == "x-repeat") ++repeats;
        Require(repeats == 2, "Repeated imported headers were lost");
    });
    test("OpenAPI 3.0 fixture resolves local parameters, request bodies and schema examples", []
    {
        const auto value = ImportOpenApi(Fixture("openapi-30.json"));
        Require(value.items.size() == 3 && value.items[0].request->method == "GET" && value.items[1].request->method == "DELETE" &&
            value.items[2].request->method == "POST", "OAS 3.0 operations missing");
        const auto get = ResolveDraft(*value.items[0].request, value.variables);
        Require(get.url == "http://127.0.0.1:8080/inspect/item%2F1?q=a%20b%26c" && get.headersText == "X-Example: fixture", "Parameter precedence or encoding failed");
        Require(Json::parse(value.items[2].request->body)["count"] == 2 && value.items[2].request->headersText == "Content-Type: application/json", "Referenced schema example missing");
    });
    test("OpenAPI 3.1 fixture selects operation/path server overrides and leaves missing examples blank", []
    {
        const auto value = ImportOpenApi(Fixture("openapi-31.json"));
        Require(value.items.size() == 3 && value.items[0].request->method == "PUT" && value.items[1].request->method == "PATCH" &&
            value.items[2].request->method == "OPTIONS", "OAS 3.1 operations missing");
        Require(ResolveDraft(*value.items[0].request, value.variables).url == "http://127.0.0.1:8080/inspect" &&
            ResolveDraft(*value.items[1].request, value.variables).url == "http://127.0.0.1:8080/path/inspect" && value.items[1].request->body.empty() &&
            Json::parse(value.items[0].request->body)["enabled"] == true && Has(value.compatibilityNotes, "without an explicit"), "OAS 3.1 mapping incorrect");
        ExportForSchema("openapi-mapped.postman.json", ExportPostman(value).text);
    });
    test("OpenAPI escaped JSON Pointers and chained local references resolve", []
    {
        auto root = MinimalApi();
        root["components"]["parameters"]["a/b~c"] = {{"name", "q"}, {"in", "query"}, {"example", "yes"}};
        root["components"]["parameters"]["Alias"] = {{"$ref", "#/components/parameters/a~1b~0c"}};
        root["paths"]["/inspect"]["get"]["parameters"] = Json::array({{{"$ref", "#/components/parameters/Alias"}}});
        const auto value = ImportOpenApi(root.dump());
        Require(value.items[0].request->url.ends_with("?q=yes"), "Escaped/chained local ref failed");
    });
    test("OpenAPI cyclic, missing and external references are reported without fetching", [&]
    {
        HttpRequest counter; counter.url = base + "/import-ref-count";
        const auto initial = Execute(counter);
        Require(!initial.error && initial.status == 200, "Reference counter fixture unavailable");
        const auto before = initial.body;
        auto root = MinimalApi();
        root["components"]["parameters"]["Cycle"] = {{"$ref", "#/components/parameters/Cycle"}};
        root["paths"]["/inspect"]["get"]["parameters"] = Json::array({{{"$ref", base + "/import-ref"}},
            {{"$ref", "file:///NEVER_READ_SYNTHETIC_FILE"}}, {{"$ref", "#/missing"}}, {{"$ref", "#/components/parameters/Cycle"}}});
        const auto value = ImportOpenApi(root.dump());
        Require(value.items.size() == 1 && Has(value.compatibilityNotes, "never fetched") && Has(value.compatibilityNotes, "Cyclic") &&
            Has(value.compatibilityNotes, "missing"), "Reference warnings missing");
        const auto after = Execute(counter);
        Require(!after.error && after.status == 200 && before == after.body, "Import fetched a reference");
    });
    test("OpenAPI parameter and JSON examples become a real loopback request", [&]
    {
        auto root = Json::parse(Fixture("openapi-30.json")); root["servers"] = Json::array({{{"url", base}}});
        auto& post = root["paths"]["/inspect"]["post"];
        post["parameters"] = Json::array({{{"name", "q"}, {"in", "query"}, {"example", "hello world"}}});
        const auto value = ImportOpenApi(root.dump());
        const auto response = SendDraft(*value.items[2].request, value.variables);
        const auto wire = Json::parse(response.body);
        Require(wire["method"] == "POST" && wire["query"] == "q=hello%20world", "OAS request did not transmit as mapped");
    });
    test("OpenAPI conservative placeholders and complex styles report their limitations", []
    {
        auto root = MinimalApi();
        root["paths"] = Json::parse(R"({"/inspect/{id}":{"get":{"responses":{"200":{"description":"OK"}},
            "parameters":[{"name":"list","in":"query","style":"deepObject"},
                {"name":"X-Blank","in":"header","schema":{"type":"string"}},
                {"name":"cookie","in":"cookie","example":"ignored"}]}}})");
        const auto value = ImportOpenApi(root.dump());
        Require(value.variables.size() == 3 && value.items[0].request->url.find("{{param") != std::string::npos &&
            Has(value.compatibilityNotes, "Complex") && Has(value.compatibilityNotes, "Cookie") && Has(value.compatibilityNotes, "empty collection"), "Placeholder/limitation policy failed");
    });
    test("OpenAPI malformed roots, versions, types and header injection fail safely", []
    {
        Reject([] { ImportOpenApi("openapi: 3.0.4"); });
        Reject([] { ImportOpenApi("{\"openapi\":\"3.1.0\",\"openapi\":\"3.0.0\"}"); });
        for (const auto* version : {"2.0", "3.2.0", "3.1.bad", "3.0."})
        { auto root = MinimalApi(); root["openapi"] = version; Reject([&] { ImportOpenApi(root.dump()); }); }
        auto root = MinimalApi(); root["paths"] = Json::array(); Reject([&] { ImportOpenApi(root.dump()); });
        root = MinimalApi(); root["paths"]["/inspect"]["get"]["parameters"] = Json::array({{{"name", "X-Bad"}, {"in", "header"}, {"example", "one\r\nInjected: two"}}});
        Reject([&] { ImportOpenApi(root.dump()); });
    });
    test("OpenAPI alternate servers, first schema example, missing base and TRACE are explicit", []
    {
        auto root = MinimalApi();
        root["servers"].push_back({{"url", "http://127.0.0.1:8081"}});
        root["paths"]["/inspect"]["trace"] = {{"responses", {{"200", {{"description", "OK"}}}}}};
        root["paths"]["/inspect"]["get"]["requestBody"] = {{"content", {{"application/json", {{"schema", {{"examples", Json::array({{{"first", true}}, {{"second", true}}})}}}}}}}};
        auto value = ImportOpenApi(root.dump());
        Require(value.items.size() == 1 && Has(value.compatibilityNotes, "first server") && Has(value.compatibilityNotes, "TRACE") &&
            Has(value.compatibilityNotes, "first schema example") && Json::parse(value.items[0].request->body)["first"] == true, "Alternative mappings were not reported");
        root.erase("servers");
        value = ImportOpenApi(root.dump());
        Require(value.variables[0].value.empty() && Has(value.compatibilityNotes, "absolute HTTP(S)"), "Missing server was silently invented");
    });
    test("Interop rejects oversized input before parsing", []
    {
        Reject([] { ImportPostman(std::string(32 * 1024 * 1024 + 1, ' ')); });
    });
    test("Interop rejects excessive JSON nesting", []
    {
        Reject([] { ImportOpenApi(std::string(100, '[') + "0" + std::string(100, ']')); });
    });
    test("Postman rejects excessive folder depth", []
    {
        // Construct bounded source text directly; avoid repeatedly copying an
        // increasingly deep JSON value just to prepare an invalid-input fixture.
        std::string text = R"({"info":{"name":"Deep","schema":"https://schema.getpostman.com/json/collection/v2.1.0/collection.json"},"item":[)";
        for (int i = 0; i < 34; ++i) text += R"({"name":"folder","item":[)";
        text += R"({"name":"leaf","item":[]})";
        for (int i = 0; i < 34; ++i) text += "]}";
        text += "]}";
        Reject([&] { ImportPostman(text); });
    });
    test("Postman rejects excessive item counts", []
    {
        std::string text = R"({"info":{"name":"Wide","schema":"https://schema.getpostman.com/json/collection/v2.1.0/collection.json"},"item":[)";
        for (int i = 0; i < 10001; ++i)
        {
            if (i) text += ',';
            text += R"({"name":"request","request":"http://127.0.0.1/inspect"})";
        }
        text += "]}";
        Reject([&] { ImportPostman(text); });
    });
}
