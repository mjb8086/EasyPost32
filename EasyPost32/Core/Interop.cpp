#include "Interop.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

namespace easypost
{
namespace
{
using Json = nlohmann::ordered_json;
constexpr const char* PostmanSchema = "https://schema.getpostman.com/json/collection/v2.1.0/collection.json";
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Note(std::vector<std::string>& notes, const std::string& text)
{
    if (std::find(notes.begin(), notes.end(), text) == notes.end() && notes.size() < 100) notes.push_back(text);
}
std::string Text(const Json& value)
{
    Require(value.is_string(), "Import expected a JSON text field.");
    return value.get<std::string>();
}
std::string Field(const Json& value, const char* key, const std::string& fallback = {})
{
    return value.contains(key) ? Text(value.at(key)) : fallback;
}
void Object(const Json& value) { Require(value.is_object(), "Import expected a JSON object."); }
void Array(const Json& value) { Require(value.is_array(), "Import expected a JSON array."); }
bool Flag(const Json& value, const char* key)
{
    if (!value.contains(key)) return false;
    Require(value.at(key).is_boolean(), "Import expected a boolean flag.");
    return value.at(key).get<bool>();
}
class ImportValidator : public nlohmann::json_sax<Json>
{
public:
    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool string(string_t& value) override { ValidateText(value); return true; }
    bool binary(binary_t&) override { return true; } // Not emitted by the JSON reader.
    bool start_object(std::size_t) override { return Start(true); }
    bool start_array(std::size_t) override { return Start(false); }
    bool end_object() override { m_keys.pop_back(); return true; }
    bool end_array() override { m_keys.pop_back(); return true; }
    bool key(string_t& value) override
    {
        ValidateText(value);
        Require(m_keys.back()->insert(value).second, "Duplicate JSON keys in import.");
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const Json::exception&) override { return false; }
private:
    std::vector<std::optional<std::set<std::string>>> m_keys;
    static void ValidateText(const std::string& value)
    { Require(value.find('\0') == value.npos, "NUL text is unsupported in imports."); }
    bool Start(bool object)
    {
        Require(m_keys.size() < 96, "Import exceeds the JSON nesting limit.");
        if (object) m_keys.emplace_back(std::in_place); else m_keys.emplace_back(std::nullopt);
        return true;
    }
};
Json Parse(const std::string& text)
{
    Require(text.size() <= 32 * 1024 * 1024, "Import exceeds the 32 MiB limit.");
    // Validate without building a tree, then preserve source object ordering.
    // DOM filter callbacks scan the growing parent array after every object;
    // avoiding them keeps large request lists from becoming quadratic.
    ImportValidator validator;
    Require(Json::sax_parse(text, &validator), "Invalid JSON or UTF-8 in import.");
    return Json::parse(text);
}
bool Method(const std::string& method)
{
    static const std::set<std::string> methods{ "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS" };
    return methods.contains(method);
}
void Unknown(const Json& value, std::initializer_list<const char*> allowed, std::vector<std::string>& notes)
{
    Object(value);
    for (const auto& [key, ignored] : value.items())
    {
        (void)ignored;
        if (std::none_of(allowed.begin(), allowed.end(), [&key](const char* entry) { return key == entry; }))
            Note(notes, "Additional source metadata or extension fields are not retained.");
    }
}
std::string Description(const Json& value, std::vector<std::string>& notes)
{
    if (!value.contains("description") || value.at("description").is_null()) return {};
    const auto& description = value.at("description");
    if (description.is_string()) return Text(description);
    Object(description);
    Note(notes, "Description text is retained; description formatting/version metadata is omitted.");
    return Field(description, "content");
}
void PostmanUnsupported(const Json& value, std::vector<std::string>& notes)
{
    if (value.contains("event") && !value.at("event").empty())
        Note(notes, "Postman scripts/tests are omitted and are never executed.");
    if (value.contains("auth") && !value.at("auth").is_null())
    {
        Object(value.at("auth"));
        if (Field(value.at("auth"), "type") != "noauth")
            Note(notes, "Postman authentication helpers/inheritance are omitted; configure explicit headers before Send.");
    }
    if (value.contains("protocolProfileBehavior") || value.contains("proxy") || value.contains("certificate"))
        Note(notes, "Postman protocol, proxy and certificate settings are omitted. TLS validation remains enabled.");
}
// Encode scalar parameter bytes, retaining EasyPost32/Postman {{variables}} verbatim.
std::string Encode(const std::string& text)
{
    static const char* digits = "0123456789ABCDEF";
    std::string result;
    for (size_t index = 0; index < text.size(); ++index)
    {
        if (text.compare(index, 2, "{{") == 0)
        {
            const auto end = text.find("}}", index + 2);
            if (end != text.npos) { result.append(text, index, end + 2 - index); index = end + 1; continue; }
        }
        const auto c = static_cast<unsigned char>(text[index]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') result += static_cast<char>(c);
        else { result += '%'; result += digits[c >> 4]; result += digits[c & 15]; }
    }
    return result;
}
void Header(std::string& target, const std::string& key, const std::string& value)
{
    Require(!key.empty() && key.find_first_of(":\r\n") == key.npos && value.find_first_of("\r\n") == value.npos,
        "An imported header contains an invalid name or line break.");
    if (!target.empty()) target += "\r\n";
    target += key + ": " + value;
}
std::string Parts(const Json& value, const std::string& separator)
{
    if (value.is_string()) return Text(value);
    Array(value);
    std::string result;
    bool first = true;
    for (const auto& part : value)
    {
        if (!first) result += separator;
        result += Text(part);
        first = false;
    }
    return result;
}
std::string PostmanUrl(const Json& url, std::vector<std::string>& notes)
{
    if (url.is_string()) return Text(url);
    Object(url);
    Unknown(url, { "raw", "protocol", "host", "port", "path", "query", "hash", "variable" }, notes);
    if (url.contains("variable") && !url.at("variable").empty())
        Note(notes, "Postman URL-scoped path variables are omitted; replace :name paths with collection {{variables}}.");
    if (url.contains("raw"))
    {
        if (url.contains("query") || url.contains("path") || url.contains("host"))
            Note(notes, "Postman raw URL is authoritative; structured URL edits/disabled query flags are not retained separately.");
        return Text(url.at("raw"));
    }
    std::string result;
    if (url.contains("protocol")) result = Text(url.at("protocol")) + "://";
    if (url.contains("host")) result += Parts(url.at("host"), ".");
    if (url.contains("port")) result += ":" + Text(url.at("port"));
    if (url.contains("path"))
    {
        auto path = Parts(url.at("path"), "/");
        if (!path.empty() && path.front() != '/') result += '/';
        result += path;
    }
    if (url.contains("query"))
    {
        Array(url.at("query"));
        bool first = true;
        for (const auto& pair : url.at("query"))
        {
            Object(pair);
            if (Flag(pair, "disabled")) { Note(notes, "Disabled Postman headers/query entries/variables are omitted."); continue; }
            result += first ? '?' : '&'; first = false;
            result += Encode(Field(pair, "key"));
            if (pair.contains("value") && !pair.at("value").is_null()) result += "=" + Encode(Text(pair.at("value")));
            Unknown(pair, { "key", "value", "disabled" }, notes);
        }
    }
    if (url.contains("hash")) result += "#" + Text(url.at("hash"));
    Note(notes, "Structured Postman URLs are flattened to editable URL text; query values are percent-encoded.");
    return result;
}
void PostmanItems(const Json& source, std::vector<CollectionItem>& items, std::vector<std::string>& notes, size_t depth, size_t& count)
{
    Array(source);
    Require(depth <= 32, "Import exceeds the 32-level folder limit.");
    Require(source.size() <= 10000 - count, "Import exceeds the 10,000-item limit.");
    for (const auto& value : source)
    {
        Require(++count <= 10000, "Import exceeds the 10,000-item limit.");
        Object(value);
        PostmanUnsupported(value, notes);
        Unknown(value, { "id", "name", "description", "item", "request", "response", "event", "auth", "variable", "protocolProfileBehavior" }, notes);
        if (value.contains("variable") && !value.at("variable").empty())
            Note(notes, "Folder/request-scoped variables are omitted; only collection-level variables are supported.");
        if (value.contains("response") && !value.at("response").empty())
            Note(notes, "Saved Postman response examples are omitted.");
        CollectionItem item;
        item.id = NewId();
        item.description = Description(value, notes);
        const auto name = Field(value, "name", "Imported request");
        Require(!name.empty(), "Imported item name is empty.");
        if (value.contains("item"))
        {
            Require(!value.contains("request"), "Postman item cannot be both a folder and a request.");
            item.folderName = name;
            PostmanItems(value.at("item"), item.items, notes, depth + 1, count);
        }
        else
        {
            RequestDraft draft{ name };
            const auto& request = value.at("request");
            if (request.is_string()) draft.url = Text(request);
            else
            {
                Object(request);
                PostmanUnsupported(request, notes);
                Unknown(request, { "url", "method", "header", "body", "description", "auth", "proxy", "certificate" }, notes);
                draft.method = Field(request, "method", "GET");
                if (!Method(draft.method)) { Note(notes, "Requests with unsupported HTTP methods are skipped."); continue; }
                draft.url = request.contains("url") ? PostmanUrl(request.at("url"), notes) : "";
                if (request.contains("description"))
                {
                    const auto description = Description(request, notes);
                    if (!item.description.empty() && item.description != description)
                        Note(notes, "Request descriptions take precedence over separate item descriptions; the latter are omitted.");
                    item.description = description;
                }
                if (request.contains("header"))
                {
                    const auto& headers = request.at("header");
                    if (headers.is_string()) draft.headersText = Text(headers);
                    else
                    {
                        Array(headers);
                        for (const auto& header : headers)
                        {
                            Object(header);
                            if (Flag(header, "disabled")) { Note(notes, "Disabled Postman headers/query entries/variables are omitted."); continue; }
                            Header(draft.headersText, Field(header, "key"), Field(header, "value"));
                            Unknown(header, { "key", "value", "disabled" }, notes);
                        }
                    }
                }
                if (request.contains("body") && !request.at("body").is_null())
                {
                    const auto& body = request.at("body");
                    Object(body);
                    if (Flag(body, "disabled")) Note(notes, "A disabled Postman body is omitted.");
                    else if (Field(body, "mode") == "raw")
                    {
                        draft.body = Field(body, "raw");
                        Unknown(body, { "mode", "raw", "disabled" }, notes);
                    }
                    else Note(notes, "Non-raw Postman body modes are omitted; the editable body is blank.");
                }
            }
            item.request = std::move(draft);
        }
        items.push_back(std::move(item));
    }
}
Json ExportItems(const std::vector<CollectionItem>& items, std::vector<std::string>& notes)
{
    auto output = Json::array();
    for (const auto& item : items)
    {
        Json value{ {"name", item.Name()} };
        if (item.request)
        {
            const auto& draft = *item.request;
            value["request"] = { {"method", draft.method}, {"url", draft.url}, {"header", draft.headersText},
                {"body", {{"mode", "raw"}, {"raw", draft.body}}} };
            if (!item.description.empty()) value["request"]["description"] = item.description;
            if (!draft.verifyTls) Note(notes, "Disabled TLS validation is not exported. Imported requests use validation enabled.");
        }
        else
        {
            value["item"] = ExportItems(item.items, notes);
            if (!item.description.empty()) value["description"] = item.description;
        }
        output.push_back(std::move(value));
    }
    return output;
}

struct OpenApi
{
    const Json& root;
    Collection collection;
    size_t count = 0;
    std::map<std::string, std::string> bases;
    void Warn(const std::string& text) { Note(collection.compatibilityNotes, text); }
    // Only root-document JSON Pointers; no URI loader exists in this library.
    const Json* Resolve(const Json& source)
    {
        const Json* value = &source;
        std::set<std::string> seen;
        for (int depth = 0; value->is_object() && value->contains("$ref"); ++depth)
        {
            const auto ref = Text(value->at("$ref"));
            if (!ref.starts_with("#/")) { Warn("External/remote or anchor references are unsupported and were never fetched."); return nullptr; }
            if (depth >= 32 || !seen.insert(ref).second) { Warn("Cyclic or overly deep local references are skipped."); return nullptr; }
            if (value->size() > 1) Warn("Reference siblings are not merged; only the referenced object is used.");
            try { value = &root.at(Json::json_pointer(ref.substr(1))); }
            catch (const Json::exception&) { Warn("A local reference is missing or has an unsupported pointer; its mapping is skipped."); return nullptr; }
        }
        return value;
    }
    void Scan(const Json& value)
    {
        if (value.is_object())
        {
            if (value.contains("$ref")) (void)Resolve(value);
            if (value.contains("$id")) Warn("JSON Schema URI scopes ($id) are unsupported; references use root-document JSON Pointers only.");
            for (const auto& [key, child] : value.items()) { (void)key; Scan(child); }
        }
        else if (value.is_array()) for (const auto& child : value) Scan(child);
    }
    const Json* Example(const Json& value)
    {
        if (value.contains("example")) return &value.at("example");
        if (value.contains("examples"))
        {
            const auto& examples = value.at("examples");
            if (examples.is_array())
            {
                if (examples.size() > 1) Warn("Only the first schema example is used.");
                if (!examples.empty()) return &examples.front();
            }
            else
            {
                Object(examples);
                if (!examples.empty())
                {
                    if (examples.size() > 1) Warn("Only the first named example is used.");
                    const auto* example = Resolve(examples.begin().value());
                    if (example)
                    {
                        Object(*example);
                        if (example->contains("value")) return &example->at("value");
                        if (example->contains("externalValue")) Warn("External example values are unsupported and were never fetched.");
                    }
                }
            }
        }
        if (value.contains("default")) return &value.at("default");
        return nullptr;
    }
    const Json* ParameterExample(const Json& parameter)
    {
        if (const auto* example = Example(parameter)) return example;
        if (parameter.contains("schema"))
        {
            const auto* schema = Resolve(parameter.at("schema"));
            if (schema && schema->is_object()) return Example(*schema);
        }
        return nullptr;
    }
    std::string Placeholder()
    {
        const auto name = "param" + std::to_string(collection.variables.size() + 1);
        collection.variables.push_back({ name, "" });
        Warn("Parameters without scalar examples use empty collection variables; fill them before Send.");
        return "{{" + name + "}}";
    }
    std::string Base(const Json& path, const Json& operation)
    {
        const Json* servers = nullptr;
        for (const auto* scope : { &operation, &path, &root })
            if (scope->contains("servers")) { servers = &scope->at("servers"); break; }
        std::string url;
        if (servers)
        {
            Array(*servers);
            if (!servers->empty())
            {
                if (servers->size() > 1) Warn("Only the first server at the applicable operation/path/root scope is used.");
                const auto& server = servers->front();
                Object(server);
                url = Text(server.at("url"));
                if (server.contains("variables"))
                {
                    Object(server.at("variables"));
                    // Expand once from the original server template, never recursively.
                    std::string expanded;
                    for (size_t pos = 0; pos < url.size();)
                    {
                        if (url[pos] != '{') { expanded += url[pos++]; continue; }
                        const auto close = url.find('}', pos + 1);
                        Require(close != url.npos, "Unclosed OpenAPI server variable.");
                        const auto key = url.substr(pos + 1, close - pos - 1);
                        const auto& var = server.at("variables").at(key);
                        expanded += Text(var.at("default")); pos = close + 1;
                    }
                    url = std::move(expanded);
                    Warn("OpenAPI server variables use their defaults; enums and alternate values are omitted.");
                }
            }
        }
        if (!url.starts_with("http://") && !url.starts_with("https://"))
            Warn("A missing/relative server needs an absolute HTTP(S) base URL in collection variables before Send.");
        if (url.find('{') != url.npos || url.find('}') != url.npos)
        { url.clear(); Warn("Unresolved server templates need an absolute base URL before Send."); }
        while (url.ends_with('/')) url.pop_back();
        auto found = bases.find(url);
        if (found != bases.end()) return "{{" + found->second + "}}";
        const auto name = bases.empty() ? "baseUrl" : "baseUrl" + std::to_string(bases.size() + 1);
        collection.variables.push_back({ name, url });
        bases[url] = name;
        return "{{" + name + "}}";
    }
    void Parameters(const Json& scope, std::vector<const Json*>& output)
    {
        if (!scope.contains("parameters")) return;
        Array(scope.at("parameters"));
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto& source : scope.at("parameters"))
        {
            const auto* value = Resolve(source);
            if (!value) continue;
            Object(*value);
            const auto key = std::make_pair(Text(value->at("name")), Text(value->at("in")));
            Require(seen.insert(key).second, "Duplicate OpenAPI parameters in a scope.");
            auto found = std::find_if(output.begin(), output.end(), [&key](const Json* entry)
            { return Field(*entry, "name") == key.first && Field(*entry, "in") == key.second; });
            if (found == output.end()) output.push_back(value); else *found = value;
        }
    }
    void Body(const Json& operation, RequestDraft& draft)
    {
        if (!operation.contains("requestBody")) return;
        const auto* body = Resolve(operation.at("requestBody"));
        if (!body) return;
        Object(*body);
        const auto& content = body->at("content"); Object(content);
        const Json* media = nullptr;
        std::string type;
        for (const auto& [key, value] : content.items())
            if (key == "application/json" || key.ends_with("+json"))
            { media = &value; type = key; if (key == "application/json") break; }
        if (!media) { Warn("Non-JSON request bodies are omitted; the editable body is blank."); return; }
        if (content.size() > 1) Warn("Only one JSON request-body media type is selected; other representations are omitted.");
        Object(*media);
        const auto* example = Example(*media);
        if (!example && media->contains("schema"))
        {
            const auto* schema = Resolve(media->at("schema"));
            if (schema && schema->is_object()) example = Example(*schema);
        }
        if (example)
        {
            // This is newly generated JSON, not imported raw source. Use native
            // edit-control line endings so the example is readable immediately.
            for (char c : example->dump(2))
            {
                if (c == '\n') draft.body += '\r';
                draft.body += c;
            }
        }
        else Warn("A JSON body without an explicit example/default stays blank; schema instances are not generated.");
        Header(draft.headersText, "Content-Type", type);
    }
    void Operation(const std::string& route, const Json& path, const std::string& method, const Json& operation)
    {
        Object(operation);
        Require(++count <= 10000, "Import exceeds the 10,000-request limit.");
        auto name = Field(operation, "summary", Field(operation, "operationId", method + " " + route));
        if (name.empty()) name = method + " " + route;
        CollectionItem item; item.id = NewId(); item.description = Field(operation, "description");
        RequestDraft draft{ name }; draft.method = method;
        std::string pathText = route;
        std::string query;
        std::vector<const Json*> parameters;
        Parameters(path, parameters); Parameters(operation, parameters);
        for (const auto* parameter : parameters)
        {
            const auto key = Text(parameter->at("name"));
            const auto location = Text(parameter->at("in"));
            if (location != "path" && location != "query" && location != "header")
            { Warn("Cookie and unsupported parameter locations are omitted."); continue; }
            const auto style = Field(*parameter, "style", location == "query" ? "form" : "simple");
            if (style != (location == "query" ? "form" : "simple") || parameter->contains("content"))
            { Warn("Complex parameter content/styles are omitted; edit the request manually."); continue; }
            const auto* example = ParameterExample(*parameter);
            if (example && !example->is_primitive())
            { Warn("Array/object parameter examples are omitted; scalar parameters are supported."); continue; }
            auto text = example ? (example->is_string() ? Text(*example) : example->dump()) : Placeholder();
            if (location == "header")
            {
                std::string lower = key;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower == "accept" || lower == "content-type" || lower == "authorization")
                { Warn("Reserved OpenAPI header parameters are ignored as specified; configure headers explicitly."); continue; }
                Header(draft.headersText, key, text);
            }
            else if (location == "query")
            {
                query += query.empty() ? '?' : '&'; query += Encode(key) + "=" + Encode(text);
                if (Flag(*parameter, "allowReserved")) Warn("allowReserved query serialization is normalized to percent-encoded scalar values.");
            }
            else
            {
                const auto token = "{" + key + "}";
                for (size_t pos = 0; (pos = pathText.find(token, pos)) != pathText.npos;)
                { auto encoded = Encode(text); pathText.replace(pos, token.size(), encoded); pos += encoded.size(); }
            }
        }
        // Convert any path token with no usable parameter to a visible editable variable.
        std::string converted;
        for (size_t pos = 0; pos < pathText.size();)
        {
            if (pathText.compare(pos, 2, "{{") == 0)
            {
                const auto close = pathText.find("}}", pos + 2);
                Require(close != pathText.npos, "Invalid path variable template.");
                converted.append(pathText, pos, close + 2 - pos); pos = close + 2;
            }
            else if (pathText[pos] == '{')
            {
                const auto close = pathText.find('}', pos + 1);
                Require(close != pathText.npos, "Unclosed OpenAPI path parameter.");
                converted += Placeholder(); pos = close + 1;
            }
            else converted += pathText[pos++];
        }
        draft.url = Base(path, operation) + converted + query;
        Body(operation, draft);
        if (operation.contains("security") || root.contains("security"))
            Warn("OpenAPI security requirements are omitted; configure authentication headers before Send.");
        item.request = std::move(draft);
        collection.items.push_back(std::move(item));
    }
};
}

Collection ImportPostman(const std::string& text)
{
    try
    {
        const auto root = Parse(text); Object(root);
        const auto& info = root.at("info"); Object(info);
        const auto schema = Text(info.at("schema"));
        Require(schema == PostmanSchema || schema == "https://schema.postman.com/json/collection/v2.1.0/collection.json",
            "Only Postman Collection v2.1 JSON is supported.");
        Collection collection; collection.id = NewId(); collection.name = Text(info.at("name"));
        auto& notes = collection.compatibilityNotes;
        collection.description = Description(info, notes);
        Unknown(root, { "info", "item", "variable", "event", "auth", "protocolProfileBehavior" }, notes);
        Unknown(info, { "name", "description", "schema", "_postman_id" }, notes);
        PostmanUnsupported(root, notes);
        if (root.contains("variable"))
        {
            Array(root.at("variable"));
            for (const auto& value : root.at("variable"))
            {
                Object(value);
                if (Flag(value, "disabled")) { Note(notes, "Disabled Postman headers/query entries/variables are omitted."); continue; }
                Variable variable{ Field(value, "key", Field(value, "id")), "" };
                if (value.contains("value"))
                {
                    const auto& contents = value.at("value");
                    if (contents.is_string()) variable.value = Text(contents);
                    else { variable.value = contents.dump(); Note(notes, "Non-text Postman variable values are converted to JSON text."); }
                }
                variable.secret = Field(value, "type") == "secret";
                if (variable.secret) Note(notes, "Marked secret variable values are session-only and will not be saved/exported.");
                Unknown(value, { "key", "id", "value", "type", "disabled" }, notes);
                collection.variables.push_back(std::move(variable));
            }
        }
        size_t count = 0;
        PostmanItems(root.at("item"), collection.items, notes, 1, count);
        Note(notes, "Postman IDs are regenerated. Environments, dynamic variables and :path variables are not evaluated.");
        (void)EncodeCollection(collection);
        return collection;
    }
    catch (const Json::exception&) { throw std::runtime_error("Invalid Postman JSON, required field, field type or UTF-8 text."); }
}
PostmanExport ExportPostman(const Collection& collection)
{
    (void)EncodeCollection(collection);
    PostmanExport result;
    Note(result.notes, "Only mapped requests, folders, descriptions and ordinary collection variables are exported. Source IDs and native history are omitted.");
    Json root{ {"info", {{"name", collection.name}, {"schema", PostmanSchema}}}, {"variable", Json::array()} };
    if (!collection.description.empty()) root["info"]["description"] = collection.description;
    for (const auto& variable : collection.variables)
    {
        if (variable.secret) { Note(result.notes, "Marked secret variables and their values are omitted. Recreate them in the destination before Send."); continue; }
        root["variable"].push_back({ {"key", variable.name}, {"value", variable.value}, {"type", "string"} });
    }
    root["item"] = ExportItems(collection.items, result.notes);
    for (const auto& note : collection.compatibilityNotes) Note(result.notes, note);
    result.text = root.dump(2) + "\n";
    Require(result.text.size() <= 32 * 1024 * 1024, "Postman export exceeds the 32 MiB limit.");
    return result;
}
Collection ImportOpenApi(const std::string& text)
{
    try
    {
        const auto root = Parse(text); Object(root);
        const auto version = Text(root.at("openapi"));
        const bool prefix = version.starts_with("3.0.") || version.starts_with("3.1.");
        Require(prefix && version.size() > 4 && std::all_of(version.begin() + 4, version.end(), [](char c) { return c >= '0' && c <= '9'; }),
            "Only OpenAPI 3.0.x and 3.1.x JSON are supported (no YAML or Swagger v2).");
        Object(root.at("info"));
        (void)Text(root.at("info").at("version"));
        Require(!version.starts_with("3.0.") || root.contains("paths"), "OpenAPI 3.0 requires a paths object.");
        OpenApi importer{ root, {} };
        importer.collection.id = NewId(); importer.collection.name = Text(root.at("info").at("title"));
        importer.collection.description = Field(root.at("info"), "description");
        importer.Scan(root);
        importer.Warn("OpenAPI is imported as editable requests only; schemas, responses, tags, callbacks, webhooks, documentation and security definitions are not retained for export.");
        if (root.contains("paths"))
        {
            Object(root.at("paths"));
            for (const auto& [route, source] : root.at("paths").items())
            {
                if (route.starts_with("x-")) continue;
                Require(route.starts_with('/'), "OpenAPI path keys must start with '/'.");
                const auto* path = importer.Resolve(source); if (!path) continue; Object(*path);
                for (const auto& [key, operation] : path->items())
                {
                    auto method = key;
                    std::transform(method.begin(), method.end(), method.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                    if (key == "get" || key == "post" || key == "put" || key == "patch" || key == "delete" || key == "head" || key == "options")
                        importer.Operation(route, *path, method, operation);
                    else if (key == "trace") importer.Warn("TRACE operations are skipped; the editor supports GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS.");
                }
            }
        }
        if (importer.collection.items.empty()) importer.Warn("No supported requests were found in this OpenAPI document.");
        (void)EncodeCollection(importer.collection);
        return std::move(importer.collection);
    }
    catch (const Json::exception&) { throw std::runtime_error("Invalid OpenAPI JSON, required field, field type or UTF-8 text."); }
}
}
