#pragma once
#include "Collection.h"

namespace easypost
{
// Pure local transformations: no file access, network access or script execution.
Collection ImportPostman(const std::string& text);
Collection ImportOpenApi(const std::string& text);
struct PostmanExport
{
    std::string text;
    std::vector<std::string> notes;
};
// The caller must present notes and obtain confirmation before writing text.
PostmanExport ExportPostman(const Collection& collection);
}
