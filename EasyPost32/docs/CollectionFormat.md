# Native collection format, version 1

Files use the `.dp32.json` suffix and UTF-8 JSON. The top-level object contains
exactly `format`, `version`, `id`, `name`, `variables` and `items`.
`format` is `EasyPost32.collection`; `version` is the integer `1`.

```json
{
  "format": "EasyPost32.collection",
  "version": 1,
  "id": "example-collection",
  "name": "Example",
  "variables": [
    { "name": "baseUrl", "value": "http://127.0.0.1:8080", "secret": false },
    { "name": "token", "secret": true }
  ],
  "items": [
    {
      "id": "folder-1",
      "name": "Requests",
      "type": "folder",
      "items": [
        {
          "id": "request-1",
          "name": "Inspect",
          "type": "request",
          "request": {
            "method": "POST",
            "url": "{{baseUrl}}/inspect",
            "headersText": "Content-Type: application/json\r\nAuthorization: Bearer {{token}}",
            "body": "{\r\n  \"example\" : true\r\n}\r\n",
            "verifyTls": true
          }
        }
      ]
    }
  ]
}
```

`items` arrays preserve sibling ordering. A folder has `id`, `name`, `type:
"folder"` and `items`; it cannot contain a `request` property. A request has
`id`, `name`, `type: "request"` and `request`; it cannot contain child `items`.
Every identifier is a nonempty string of at most 128 bytes and is unique across
the collection and all descendants. Names are nonempty strings. New and
duplicated items receive fresh identifiers, including all duplicated children.
The UI reorders siblings; moving an existing item between folders is deferred.

The request object contains exactly `method`, `url`, `headersText`, `body` and
`verifyTls`. Supported methods are GET, POST, PUT, PATCH, DELETE, HEAD and OPTIONS.
URLs, header source and body are strings. Header source is retained verbatim,
including repeated headers and incomplete lines, so unfinished edits can be
saved. It is parsed and validated when sending. Body text is never JSON-parsed,
reformatted or re-serialized before transmission. `verifyTls` is a required
boolean and controls both certificate-chain and hostname checks. New requests
default to true; reopening retains an explicitly saved false setting and its
visible warning.

The reader rejects unknown fields, unknown versions, duplicate JSON keys,
duplicate identifiers, invalid UTF-8, embedded NUL text, incorrect field types,
files over 32 MiB, more than 10,000 items, folders deeper than 32 item levels,
and more than 1,000 variables. Empty collections and folders are supported.
Validation completes before an existing document is replaced. Save applies the
same validation, writes a unique temporary file beside the destination, flushes
and closes it, then uses Windows replacement/rename. Failed saves report an
error; locked-file tests verify that the previous complete file is retained.
Successful replacement removes the temporary file; failure cleanup is attempted.
An abrupt process or OS
failure may leave an unused `.dp32-*.tmp` file; these are never loaded as a
collection automatically. Filesystem/network-share durability beyond Windows'
replacement guarantees is not promised.

Responses, jobs, selection/expansion state and history are not collection data.
Collection names are independent of file names; rename the tree root to change
the stored collection name. Closing a modified document offers Save, Discard
or Cancel through the normal MFC document workflow.

## Variables and secrets

Variable names are case sensitive and consist of 1–64 ASCII letters, digits,
dots, underscores or hyphens. Duplicate names are rejected. Each row requires a
boolean `secret`. Ordinary rows require `value`; secret rows must omit `value`.
Native files containing a value on a marked secret row are rejected.

Use `{{name}}` in a URL, header source or raw body. Expansion scans the original
source once at Send, into a separate snapshot. Missing names, malformed braces,
empty required secret values and nested placeholders in used values fail before
starting a worker. There is no escape syntax or recursive expansion. Variable
values inserted into URLs/headers may not contain CR, LF or NUL. Body values can
contain CR/LF. Substitution performs no URL encoding: encode query/path values
explicitly when needed. Expanded fields are limited to 32 MiB each.

Marked values remain in memory for this application session/document. Saving
omits them without changing the live values. Reopening leaves them empty and
requires re-entry before a request that uses them can be sent. This is not
encrypted secret storage or Windows Credential Manager integration.

**Ordinary variables, URLs, headers and bodies are plaintext in saved files.**
Marking a variable secret cannot protect a credential separately typed into a
literal header or body. Use secret placeholders and inspect files before sharing.
The editor and Variables dialog disclose the plaintext behavior. The Variables
dialog edits one value per row; its text field is single-line and masks secret
values. Multiline ordinary values can be authored in the JSON file if needed.

## Local history

File > Local History controls a session-only opt-in, initially off. It records
completed sends while their collection remains open. Recording must be enabled
both at Send and at completion. There is no automatic replay.

The file is `%LOCALAPPDATA%\EasyPost32\history.json`, with `format:
"EasyPost32.history"`, `version: 1`, and an `entries` array. At most the newest
500 entries remain. Each entry contains exactly:

- `timestamp`: UTC `YYYY-MM-DDTHH:MM:SSZ` at Send;
- `method`: supported HTTP method;
- `url`: scheme/host/port only, or `[endpoint omitted]`;
- `status`: HTTP status or zero if no HTTP response;
- `elapsedMs`: elapsed milliseconds;
- `transportError`: boolean, without diagnostic contents.

User information, URL paths, queries and fragments are omitted because they may
contain tokens. The entire endpoint is omitted for any collection containing a
marked secret variable, including secrets used to build a hostname. Invalid or
oversized origins are also omitted. Request/response headers, bodies, credential
values, variable names/values and full URLs are never history fields. The store
sanitizes URLs again at its write boundary. Literal hostnames that a user types
literally can still be visible as metadata. History is plaintext local metadata,
bounded and atomically replaced. Clear History empties both memory and disk;
it does not promise forensic erasure of filesystem backups or old disk blocks.

History is loaded only when its dialog is opened and is never a startup
dependency. Errors leave visible entries intact and can be cleared in the
dialog. A write failure disables recording and reports the problem without
discarding the HTTP response. GUI tests override `LOCALAPPDATA` in their private
child process, so they never modify the developer's real history.
