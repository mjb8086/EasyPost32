#include "Collection.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace easypost
{
using nlohmann::json;
namespace
{
void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
bool VariableName(std::string_view name)
{
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    return true;
}
void Keys(const json& object, std::initializer_list<const char*> allowed)
{
    Check(object.is_object(), "Expected a JSON object.");
    for (const auto& [key, value] : object.items())
    {
        (void)value;
        Check(std::any_of(allowed.begin(), allowed.end(), [&key](const char* name) { return key == name; }),
            "Unknown collection field; refusing a potentially lossy load.");
    }
}
std::string Text(const json& value, bool nonempty = false)
{
    Check(value.is_string(), "Expected a text field.");
    auto text = value.get<std::string>();
    Check(text.find('\0') == text.npos && (!nonempty || !text.empty()), "Invalid empty or NUL-containing text field.");
    return text;
}
CollectionItem ReadItem(const json& value, size_t depth, size_t& count, std::set<std::string>& ids)
{
    Check(depth <= 32 && ++count <= 10000, "Collection exceeds the folder depth or item limit.");
    Keys(value, { "id", "type", "name", "items", "request" });
    CollectionItem item;
    item.id = Text(value.at("id"), true);
    Check(item.id.size() <= 128 && ids.insert(item.id).second, "Invalid or duplicate item identifier.");
    const auto name = Text(value.at("name"), true);
    const auto type = Text(value.at("type"));
    if (type == "folder")
    {
        Check(!value.contains("request") && value.at("items").is_array(), "Invalid folder fields.");
        item.folderName = name;
        for (const auto& child : value.at("items")) item.items.push_back(ReadItem(child, depth + 1, count, ids));
    }
    else
    {
        Check(type == "request" && !value.contains("items"), "Invalid item type or request fields.");
        const auto& fields = value.at("request");
        Keys(fields, { "method", "url", "headersText", "body", "verifyTls" });
        RequestDraft draft;
        draft.name = name;
        draft.method = Text(fields.at("method"));
        static const std::set<std::string> methods{ "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS" };
        Check(methods.contains(draft.method), "Unsupported request method in native collection.");
        draft.url = Text(fields.at("url"));
        draft.headersText = Text(fields.at("headersText"));
        draft.body = Text(fields.at("body"));
        Check(fields.at("verifyTls").is_boolean(), "Expected a boolean TLS setting.");
        draft.verifyTls = fields.at("verifyTls").get<bool>();
        item.request = std::move(draft);
    }
    return item;
}
json WriteItem(const CollectionItem& item, size_t depth, size_t& count)
{
    Check(depth <= 32 && ++count <= 10000, "Collection exceeds the folder depth or item limit.");
    Check(!item.request || item.items.empty(), "Requests cannot contain child items.");
    json value{ {"id", item.id}, {"name", item.Name()}, {"type", item.request ? "request" : "folder"} };
    if (item.request)
    {
        const auto& draft = *item.request;
        value["request"] = { {"method", draft.method}, {"url", draft.url}, {"headersText", draft.headersText},
            {"body", draft.body}, {"verifyTls", draft.verifyTls} };
    }
    else
    {
        value["items"] = json::array();
        for (const auto& child : item.items) value["items"].push_back(WriteItem(child, depth + 1, count));
    }
    return value;
}
std::string Expand(const std::string& source, const std::vector<Variable>& variables, bool singleLine)
{
    std::string output;
    for (size_t pos = 0; pos < source.size();)
    {
        const auto open = source.find("{{", pos);
        const auto stray = source.find("}}", pos);
        if (stray != source.npos && (open == source.npos || stray < open))
            throw std::runtime_error("Unmatched variable delimiter.");
        if (open == source.npos) { output.append(source, pos); break; }
        output.append(source, pos, open - pos);
        const auto close = source.find("}}", open + 2);
        Check(close != source.npos, "Unclosed variable placeholder.");
        const auto name = source.substr(open + 2, close - open - 2);
        Check(VariableName(name), "Invalid variable name in a placeholder.");
        const auto found = std::find_if(variables.begin(), variables.end(), [&name](const auto& var) { return var.name == name; });
        if (found == variables.end()) throw std::runtime_error("Unresolved variable: " + name);
        Check(!found->secret || !found->value.empty(), "A required secret variable needs a session value.");
        Check(found->value.find("{{") == source.npos && found->value.find("}}") == source.npos,
            "Nested variable placeholders are unsupported; use a literal value.");
        Check(found->value.find('\0') == source.npos && (!singleLine || found->value.find_first_of("\r\n") == source.npos),
            "A variable contains invalid control characters for a URL or header.");
        Check(output.size() + found->value.size() <= 32 * 1024 * 1024, "Expanded request exceeds the size limit.");
        output += found->value;
        pos = close + 2;
    }
    Check(output.size() <= 32 * 1024 * 1024, "Expanded request exceeds the size limit.");
    return output;
}
}

std::string NewId()
{
    std::random_device random;
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) result << std::setw(8) << random();
    return result.str();
}
Collection NewCollection(const std::string& name)
{
    Collection collection{ NewId(), name };
    for (const auto* requestName : { "Request 1", "Request 2" })
    {
        CollectionItem item;
        item.id = NewId();
        item.request = RequestDraft{ requestName };
        collection.items.push_back(std::move(item));
    }
    return collection;
}
CollectionItem* FindItem(std::vector<CollectionItem>& items, const std::string& id)
{
    for (auto& item : items)
    {
        if (item.id == id) return &item;
        if (auto* found = FindItem(item.items, id)) return found;
    }
    return nullptr;
}
std::vector<CollectionItem>* ItemSiblings(std::vector<CollectionItem>& items, const std::string& id)
{
    for (auto& item : items)
    {
        if (item.id == id) return &items;
        if (auto* found = ItemSiblings(item.items, id)) return found;
    }
    return nullptr;
}
std::string FirstRequestId(const std::vector<CollectionItem>& items)
{
    for (const auto& item : items)
    {
        if (item.request) return item.id;
        auto nested = FirstRequestId(item.items);
        if (!nested.empty()) return nested;
    }
    return {};
}
void RenewIds(CollectionItem& item)
{
    item.id = NewId();
    for (auto& child : item.items) RenewIds(child);
}
void ValidateVariables(const std::vector<Variable>& variables)
{
    Check(variables.size() <= 1000, "Too many collection variables.");
    std::set<std::string> names;
    for (const auto& variable : variables)
    {
        Check(VariableName(variable.name), "Variable names require 1-64 ASCII letters, digits, dots, underscores or hyphens.");
        Check(names.insert(variable.name).second, "Duplicate variable names are not allowed (case sensitive).");
        Check(variable.value.find('\0') == variable.value.npos, "Variable values cannot contain NUL.");
    }
}
RequestDraft ResolveDraft(const RequestDraft& draft, const std::vector<Variable>& variables)
{
    ValidateVariables(variables);
    auto resolved = draft;
    resolved.url = Expand(draft.url, variables, true);
    resolved.headersText = Expand(draft.headersText, variables, true);
    resolved.body = Expand(draft.body, variables, false);
    return resolved;
}
Collection DecodeCollection(const std::string& jsonText)
{
    Check(jsonText.size() <= 32 * 1024 * 1024, "Collection file exceeds 32 MiB.");
    try
    {
        std::vector<std::set<std::string>> keys;
        const auto root = json::parse(jsonText, [&keys](int depth, json::parse_event_t event, json& parsed)
        {
            Check(depth <= 96, "JSON nesting exceeds the collection limit.");
            if (event == json::parse_event_t::object_start) keys.emplace_back();
            if (event == json::parse_event_t::key)
                Check(keys.back().insert(parsed.get<std::string>()).second, "Duplicate JSON keys are not allowed.");
            if (event == json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
        Keys(root, { "format", "version", "id", "name", "variables", "items" });
        Check(root.at("format") == "EasyPost32.collection", "Not an EasyPost32 collection.");
        Check(root.at("version").is_number_integer() && root.at("version") == 1, "Unsupported collection version.");
        Collection collection;
        collection.id = Text(root.at("id"), true);
        Check(collection.id.size() <= 128, "Collection identifier is too long.");
        collection.name = Text(root.at("name"), true);
        Check(root.at("variables").is_array() && root.at("items").is_array(), "Expected item and variable arrays.");
        for (const auto& field : root.at("variables"))
        {
            Keys(field, { "name", "value", "secret" });
            Variable variable;
            variable.name = Text(field.at("name"), true);
            Check(field.at("secret").is_boolean(), "Expected a boolean secret flag.");
            variable.secret = field.at("secret").get<bool>();
            Check(!variable.secret || !field.contains("value"), "Secret values must not be stored in collection files.");
            if (!variable.secret) variable.value = Text(field.at("value"));
            collection.variables.push_back(std::move(variable));
        }
        ValidateVariables(collection.variables);
        size_t count = 0;
        std::set<std::string> ids{ collection.id };
        for (const auto& item : root.at("items")) collection.items.push_back(ReadItem(item, 1, count, ids));
        return collection;
    }
    catch (const json::exception&) { throw std::runtime_error("Invalid collection JSON, field type or UTF-8 text."); }
}
std::string EncodeCollection(const Collection& collection)
{
    ValidateVariables(collection.variables);
    json root{ {"format", "EasyPost32.collection"}, {"version", 1}, {"id", collection.id}, {"name", collection.name},
        {"variables", json::array()}, {"items", json::array()} };
    for (const auto& variable : collection.variables)
    {
        json value{ {"name", variable.name}, {"secret", variable.secret} };
        if (!variable.secret) value["value"] = variable.value;
        root["variables"].push_back(std::move(value));
    }
    size_t count = 0;
    for (const auto& item : collection.items) root["items"].push_back(WriteItem(item, 1, count));
    auto encoded = root.dump(2) + "\n";
    (void)DecodeCollection(encoded); // Apply the same validation before any disk write.
    return encoded;
}
}
