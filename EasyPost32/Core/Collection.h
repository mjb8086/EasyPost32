#pragma once
#include "../RequestDraft.h"
#include <optional>
#include <vector>

namespace easypost
{
struct Variable
{
    std::string name, value;
    bool secret = false;
    bool operator==(const Variable&) const = default;
};
struct CollectionItem
{
    std::string id;
    std::string folderName;
    std::optional<RequestDraft> request;
    std::vector<CollectionItem> items;
    const std::string& Name() const { return request ? request->name : folderName; }
    bool operator==(const CollectionItem&) const = default;
};
struct Collection
{
    std::string id, name;
    std::vector<CollectionItem> items;
    std::vector<Variable> variables;
    bool operator==(const Collection&) const = default;
};
std::string NewId();
Collection NewCollection(const std::string& name);
CollectionItem* FindItem(std::vector<CollectionItem>& items, const std::string& id);
std::vector<CollectionItem>* ItemSiblings(std::vector<CollectionItem>& items, const std::string& id);
std::string FirstRequestId(const std::vector<CollectionItem>& items);
void RenewIds(CollectionItem& item);
void ValidateVariables(const std::vector<Variable>& variables);
// One pass over the original templates. Nested placeholders in used values fail.
RequestDraft ResolveDraft(const RequestDraft& draft, const std::vector<Variable>& variables);
std::string EncodeCollection(const Collection& collection);
Collection DecodeCollection(const std::string& jsonText);
}
