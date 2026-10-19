#include "RequestComposer.h"
#include <string_view>

bool ComposeRequest(const RequestDraft& draft, easypost::HttpRequest& request, std::string& error)
{
    request = {};
    error.clear();
    request.method = draft.method;
    request.url = draft.url;
    if (!draft.body.empty() || draft.method == "POST" || draft.method == "PUT" || draft.method == "PATCH")
        request.body = draft.body;
    std::string_view remaining(draft.headersText);
    for (size_t lineNumber = 1; !remaining.empty(); ++lineNumber)
    {
        const auto newline = remaining.find('\n');
        auto line = remaining.substr(0, newline);
        remaining = newline == remaining.npos ? std::string_view{} : remaining.substr(newline + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        const auto colon = line.find(':');
        if (colon == line.npos || colon == 0)
        {
            error = "Header line " + std::to_string(lineNumber) + " must use Name: value.";
            return false;
        }
        auto value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
        request.headers.emplace_back(line.substr(0, colon), value);
    }
    return true;
}
