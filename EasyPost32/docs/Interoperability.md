# JSON interoperability (Slice 6)

Use **File > Import > Postman v2.1 JSON** or **OpenAPI 3.0/3.1 JSON**. Import
reads one local UTF-8 file and shows a scrollable compatibility report. Continue
creates a new, independent, modified MDI collection with no native filename;
Save asks for a `.dp32.json` destination. Cancel creates no document. Existing
documents and source files are untouched. Nothing is sent until you press Send.

**File > Export Postman v2.1 JSON** shows its compatibility report before the
destination chooser. Cancel writes nothing. Export uses the atomic writer,
does not rename/save the native document and does not clear its modified flag.
The reports include losses remembered from import, including after native
save/reopen. They describe categories, without echoing source credentials.

## Postman Collection v2.1

The `info.schema` URI must identify the official v2.1.0 collection schema
(`schema.getpostman.com` or `schema.postman.com`, HTTPS). Other versions, YAML
folder layouts, environments and workspaces are unsupported.

| Source | Mapping |
| --- | --- |
| Collection name/description | Native name and plain description text |
| Nested `item` groups | Ordered folders and requests with fresh native IDs |
| Request name/method | GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS; other methods skipped with warning |
| String request | GET with that literal URL |
| String URL / structured `url.raw` | Exact editable URL text; raw takes precedence over separate structured fields |
| Structured URL without raw | Protocol, host string/array, port, path string/array, ordered repeated query entries, fragment |
| Headers | Ordered enabled header pairs or verbatim header-source string; repeated names retained |
| Raw body | Exact decoded UTF-8 string, including whitespace and final newlines |
| Collection variables | Unique names compatible with native variable rules; strings retained, other JSON values converted to JSON text |
| Descriptions | Text retained; object format/version metadata omitted and reported; request descriptions override separate item descriptions with a warning |

Structured query keys/values are treated as unencoded scalars and percent-encoded
as UTF-8; `{{variables}}` are preserved. Host/path segments are joined verbatim.
If the original uses pre-encoded query values, review the flattened result.
When `raw` exists it is authoritative, including query contents; disabled query
flags in a separate URL representation are not applied and this is reported.
Disabled headers, collection variables and structured queries are omitted with
warnings; disabled bodies stay blank. URLs with `:path` variables, URL-scoped or
folder/request-scoped variables, environments and dynamic variables require
manual conversion to native collection variables. Undefined `{{variables}}`
continue to fail before Send.

Scripts/tests are discarded as data and never evaluated. Authentication helpers
and inheritance, protocol/proxy/client-certificate settings, response examples,
special body modes (multipart, form URL encoding, files, GraphQL), and extra
metadata/extensions are omitted and reported. No referenced upload file is read.
Raw-body language/options metadata and Postman's implicit headers are not
reconstructed; set Content-Type explicitly when needed. Source IDs are
regenerated. A caller should review every compatibility report before sending
an imported request, especially when an omitted feature affected its semantics.

Export writes the official v2.1 shape with nested items, names/descriptions,
method, raw URL, header-source string, raw body and ordinary string variables.
This mapping is validated against the checked-in official Draft 4 schema in
tests. Supported fields survive import → export → import; original structured
representation, source IDs and unsupported data do not. The import parser is a
bounded subset parser, not a complete Postman-schema validator.

TLS validation is always enabled on imported requests. A native disabled-TLS
setting has no exported mapping and is explicitly reported. Marked secret
variables **including their names/values** are omitted from Postman export;
recreate them in the destination. Import recognizes the commonly encountered
nonstandard `type: "secret"` as a session-only value, but never emits that
schema-invalid variable type. Ordinary values/headers/URLs/bodies are plaintext:
credentials typed directly into those fields remain visible in saved/exported
JSON. Secret classification is never guessed from a name.

## OpenAPI 3.0.x / 3.1.x JSON

Paths are traversed in source order, operations become flat selectable requests
in that order, and summaries or operation IDs supply names (otherwise METHOD
plus path). Collection title/description and operation descriptions are retained.
Supported operations are GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS; TRACE is reported
and skipped. This creates an editable request collection, not a preserved OAS
document, and OpenAPI export is unavailable.

The first server is chosen using operation > path > root precedence. Alternate
servers are reported. Server variables are substituted once using their default
values, then stored as an editable `baseUrl` collection variable (additional
distinct bases use `baseUrl2`, etc.). Missing/relative/unresolved server URLs
need an absolute HTTP(S) value before Send; no source-directory URL or remote
server is inferred. Defaults are not recursively evaluated.

Path-level parameters merge with operation parameters; matching `(name,in)`
entries are overridden at the operation. Duplicate entries within one scope are
rejected. Scalar path, query and header examples/defaults are supported with
`simple` path/header or `form` query serialization. URL values are percent-encoded
as UTF-8. OpenAPI reserved header parameters (Accept, Content-Type, Authorization)
are ignored and reported. Cookies, content parameters, complex styles, arrays
and objects are reported as unsupported. `allowReserved` is normalized to encoded
scalars and reported. Parameters without examples become empty `paramN`
collection variables; fill and encode replacement values appropriately before
Send. Their names are generated rather than copied from possibly invalid native
variable names. Missing path parameter definitions also get these placeholders.

JSON bodies select `application/json`, otherwise the first `+json` content type.
That Content-Type is added explicitly. Media-type examples, first named examples,
or referenced schema examples/defaults supply an editable JSON string. OAS 3.1
schema `examples` uses its first value. Values are serialized as JSON, including
quotes for a JSON string example and CRLF formatting for the native editor.
A later Send preserves the resulting editor
text exactly. Without an explicit example/default the body stays blank; schema
properties are never used to invent an instance. Alternate examples/media types
and non-JSON bodies are omitted/reported.

Only root-document `#/...` JSON Pointers are resolved (including `~0` / `~1`),
for path items, parameters, request bodies, examples and schemas used above.
Reference chains are limited to 32; cycles, missing targets, unsupported pointer
encodings and reference siblings are reported. External files, HTTP(S) refs,
anchors, nested `$id` URI scopes and external example values are unsupported;
**nothing is fetched or opened**. JSON Schema 3.1 `$id` scoping is not implemented:
local pointers are interpreted from the root document and this limitation is
reported when `$id` appears. Schema constraints, composition and property-level
instance generation are not applied. Security requirements, schema/response
definitions, tags, callbacks/webhooks, documentation and extensions are not
retained for export; the compatibility report says so.

## Bounds and validation

Imports reject malformed JSON, invalid UTF-8/NUL strings, duplicate keys, wrong
required field types, unsupported format versions and unrepresentable native
values before creating a document. Limits are 32 MiB, 96 JSON nesting levels,
32 item levels, 10,000 items and 1,000 collection variables. Header pair imports
reject CR/LF injection. Parsing and serialization use plain C++ only: there is
no network, script or external-file resolver in `Core/Interop`.

## Verification and manual checks

From the project directory after `tests\SetupHttp.ps1`, run:

```powershell
powershell.exe -NoProfile -File .\tests\TestHttp.ps1 -Configuration Debug
powershell.exe -NoProfile -File .\tests\TestGuiHttp.ps1 -Configuration Debug
```

The native suite covers fixtures, supported round trips, raw bodies, disabled
entries, loss notes surviving native saves, secret/TLS export policy, OAS 3.0/3.1,
local/chained/escaped/cyclic/missing/external references, URL encoding, operation
overrides, conservative examples, malformed input and limits. Real imported
requests are sent only to private loopback fixtures. A dedicated counter proves
that importing a reachable external reference makes no HTTP request. Production
exports are checked offline against `tests/Schemas/postman-v2.1.0.json`; the
validator registry rejects external retrieval. Python/jsonschema are test-only.

For manual review, import `tests/Fixtures/postman-v21.json`, inspect Outer >
Inner > Exact JSON echo, review its variables and Save As a native document.
Import `postman-unsupported.json` and inspect the warnings before Continue/Cancel.
Export the active document and review warnings before choosing a destination.
Import `openapi-30.json` and `openapi-31.json`; check operation selection, URLs,
headers and example bodies. Set server variables to an explicitly chosen local
fixture URL before Send. Cancel import/export and verify other MDI documents
and destination files are unchanged. Imported files remain local unsaved working
documents until Save As. No product CLI, scripting runtime or YAML support was
added.
