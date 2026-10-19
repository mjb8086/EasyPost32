#pragma once
#include "RequestDraft.h"
#include "Http/HttpClient.h"

// Parse the editor's header lines without rewriting the stored draft or body.
// Diagnostics contain line numbers only, never potentially secret header values.
bool ComposeRequest(const RequestDraft& draft, easypost::HttpRequest& request, std::string& error);
