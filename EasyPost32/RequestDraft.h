#pragma once

#include <string>

// Editor text is UTF-8. Header source retains order, repeats and unfinished lines.
// This model has no MFC types, persistence or transport behavior.
struct RequestDraft
{
    std::string name;
    std::string method = "GET";
    std::string url;
    std::string headersText;
    std::string body;
    bool verifyTls = true;
    bool operator==(const RequestDraft&) const = default;
};
