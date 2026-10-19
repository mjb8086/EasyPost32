# EasyPost32 — Product & Implementation Specification

**Status:** Initial development specification (v0.1)  
**Platform:** Windows 10/11 desktop (x64-first)  
**Language:** C++20  
**UI:** MFC with native Win32 controls; classic multiple-document interface (MDI)  
**HTTP:** libcurl  
**Operating model:** Local-first; no account, cloud service or telemetry

> **One-line brief:** EasyPost32 is an old-fashioned, fast, native Windows HTTP workbench. Open a collection, edit a request, press Send, inspect the response. No sign-in required.

## 1. Product intent

EasyPost32 is a lightweight alternative to Postman for developers who need to make and organise HTTP requests, rather than participate in a hosted API collaboration platform. The experience should feel like a useful classic Windows desktop utility: menu bar, toolbar, status bar, keyboard shortcuts, resizable panes, and independent MDI child windows for collections.

### Goals

- Send ordinary HTTP requests to user-selected endpoints, with an editable method, URL, headers and raw request body.
- Show response status, headers, body, elapsed time and transfer size.
- Organise requests into named collections and nested folders, with **one MDI child document per collection**.
- Save/open local collection files; keep collection-level variables for reusing base URLs and other parameters.
- Maintain a local request history without leaking credentials by default.
- Import and export the supported subset of **Postman Collection v2.1 JSON**, and import **OpenAPI 3.0/3.1 JSON** specifications as editable collections.
- Remain useful completely offline, except when the user explicitly sends an HTTP request; no mandatory online service or authentication.

### Non-goals for v0.1

- Postman-compatible script execution, tests, environments/cloud workspaces, team collaboration or synchronisation.
- OAuth 2.0 browser flows, WebSockets, gRPC, GraphQL-specific tooling, mock servers, monitors, CLI runners, code generation, or cookies persistence across launches.
- Full OpenAPI specification fidelity (especially remote references and arbitrary schema-to-example generation).
- Rich response rendering (HTML preview, images, video), an extension system or automatic updates.
- Cross-platform support, Electron, Chromium/CEF, WebView2 as a required UI dependency, or any hosted backend.

**Scope rule:** Prefer reliable execution of common HTTP requests and a comprehensible user interface over feature parity with Postman.

## 2. User experience and application behaviour

### Main window

- A conventional Windows title/menu/toolbar/status-bar frame with an MDI client area.
- `File` menu: New Collection, Open, Save, Save As, Import, Export (when available), Exit.
- `Request` menu: Send, Cancel (when available), New Request, Duplicate, Delete.
- `Window` menu: Activate, Cascade, Tile, Close (where supported by generated MFC scaffold).
- Each open collection lives in its own MDI child and can be independently selected, rearranged, saved and closed.
- Prompt before closing a document with unsaved changes.
- Classic-looking UI is a feature, but high-DPI legibility, resizing, accessible keyboard focus, and functional controls matter more than pixel-perfect nostalgia.

### Collection child window

- **Left pane:** hierarchical folder/request tree, with method labels and request names.
- **Right pane (top):** method selector, URL field, Send/Cancel, tabs or grouped controls for Params, Headers, Body and Variables.
- **Right pane (bottom):** status, timing, response headers and response body; resizable splitter between request and response.
- Use native controls initially. A Scintilla editing control is preferred for request/response source views once the basics work, but its integration must not hold up Slice 1.
- Body editing supports plain text, including editable JSON; JSON syntax colouring and formatting are usability improvements, not a requirement for making an HTTP request.

### Required request semantics

- Support GET, POST, PUT, PATCH and DELETE; additionally HEAD and OPTIONS are low-cost once the underlying engine is in place.
- Allow editing raw URLs, repeated or arbitrary request headers, query parameters, and raw UTF-8 body text.
- Preserve the exact user-authored body text; do **not** pretty-print or re-serialize it before sending unless the user explicitly requests formatting.
- Show successful HTTP responses **and** 4xx/5xx responses as normal responses, not transport failures.
- TLS certificate verification is enabled by default and must not be silently disabled.
- Network operations must not block the MFC UI thread. Provide clear pending/completed/error states, timeouts and eventually cancellation.
- Display binary/non-text responses safely (metadata and a placeholder, rather than dumping arbitrary bytes into a text control).

## 3. Technology and architecture

| Concern | Proposed implementation | Notes |
| --- | --- | --- |
| Windows UI | MFC document/view + Win32 common controls | Use `CMultiDocTemplate`, `CDocument`, `CMDIChildWnd`/view as appropriate |
| Language | C++20, MSVC | x64 Debug and Release targets; “32” is branding, **not** a requirement to build 32-bit binaries |
| HTTP engine | libcurl easy API on background worker(s) | One easy handle must not be used concurrently across threads; libcurl multi only if needed later |
| JSON | nlohmann/json | Internal document and importer parsing |
| Code editing | Native multiline edit first; Scintilla/Lexilla later | No browser runtime |
| Persistence | Human-readable `.dp32.json` files | Source of truth; atomic save where practical |
| History | Metadata-only local JSONL, or equivalent small on-disk file | No credentials, request body or response body by default |
| Dependencies | vcpkg manifest (`vcpkg.json`) | Check in manifest and pinned baseline/config as appropriate; not vendored binaries |
| Build | Visual Studio solution/MSBuild initially | Preserve the MFC App Wizard scaffold; document reproducible build commands |
| Tests | Native unit tests + local loopback HTTP fixture | No public endpoint or internet required in CI/tests |

### Suggested boundaries (adapt to wizard-generated structure)

```text
EasyPost32/
  AGENTS.md
  SPEC.md
  EasyPost32.sln / generated MFC project(s)
  vcpkg.json
  src/
    App/               # MFC frames, views, resources, UI messaging
    Core/              # Request/response types, variable resolution
    Http/              # libcurl adapter, worker execution
    Persistence/       # .dp32.json parsing/writing, history
    Importers/         # Postman and OpenAPI adapters
  tests/
    CoreTests/
    ImporterFixtures/
    LocalHttpServer/   # Small offline loopback fixture if appropriate
  docs/
    screenshots/       # Optional later
```

A generated Visual Studio MFC wizard may create a different file layout. **Do not restructure working wizard code simply to match this illustrative tree.** Keep domain/network/import code independent from `CWnd`, `CString` and other MFC types where reasonably possible, using standard C++ strings and value types at the boundary. Avoid creating six layers of abstractions for a small utility.

### Internal data model (target for Slices 4–5)

- **Collection:** schema version, identifier, name, ordered tree of folders/requests, collection variables.
- **Folder:** identifier, name, ordered children (folders or requests).
- **Request:** identifier, name, method, URL template, ordered header pairs, optional query pairs, body mode/raw text, optional description.
- **Response:** status/status text, final URL, response headers, captured byte body, elapsed milliseconds, transfer metadata, optional transport error.
- **Variable:** name, value and optional `secret` classification.

For a simple native file example (illustrative, not an irrevocable schema):

```json
{
  "format": "Easypost32.collection",
  "version": 1,
  "id": "sample-collection",
  "name": "Example API",
  "variables": [
    { "name": "baseUrl", "value": "http://127.0.0.1:8080" }
  ],
  "items": [
    {
      "type": "request",
      "id": "sample-get",
      "name": "Get status",
      "method": "GET",
      "url": "{{baseUrl}}/status",
      "headers": [],
      "body": { "mode": "none", "raw": "" }
    }
  ]
}
```

The full schema should be specified and tested **when implementing Slice 5**, not embellished in advance. Never persist a response body in a collection document by accident.

## 4. Six implementation slices

Each slice is a **reviewable checkpoint**, not a request to implement everything at once. Complete its acceptance checks before starting the next one. Avoid introducing later-slice features prematurely.

### Slice 1 — Native MFC/MDI application scaffold

**Outcome:** A compilable, recognisably native Windows application that can open multiple empty collection documents.

**Work:**

1. Create a Visual Studio **MFC Application** using **Multiple Documents (MDI)**, C++20, x64 configuration; prefer classic MDI child windows, not forced tabbed document UI.
2. Set application title **EasyPost32**, with normal menu, toolbar, status bar and `File > New`, `File > Open` as practical.
3. Use `CMultiDocTemplate`, a collection `CDocument` derivative, an MDI child frame and view from the wizard scaffold.
4. Keep the initial collection document deliberately minimal. Show its name/title; no fabricated HTTP logic.
5. Initialise git/repository basics, add README build notes, `.gitignore`, `SPEC.md` and `AGENTS.md`.
6. Document exactly how to build and run the project on the configured machine.

**Acceptance:**

- [ ] x64 Debug builds without errors and launches under Visual Studio.
- [ ] `File > New` opens at least two independent child documents.
- [ ] The Window menu can activate/arrange child windows (or an equivalent MDI affordance).
- [ ] Closing one child does not close the application or another child.
- [ ] No login, online setup or networking is required to launch.
- [ ] Codex reports the build command, changed files and any verification not performed.

**Out of scope:** Sending requests, real collection persistence, importers.

### Slice 2 — Usable request editor (UI and in-memory state)

**Outcome:** A developer can compose a request in one collection window; the form works without HTTP transmission yet.

**Work:**

1. Add a left-hand collection tree with a sample/request node and a right-hand request editor.
2. Provide a method combo box, URL input, editable ordered headers and a raw multiline body editor (JSON-friendly text).
3. Add Send control, visibly disabled or marked unavailable until Slice 4; it must not claim to send a request.
4. Make sizing/splitters, focus order and selection-to-editor interaction usable.
5. Store editor values in a plain-C++ in-memory request model; switching request selections must not discard edits.
6. Ensure each MDI document has its own independent request editing state.

**Acceptance:**

- [ ] Method, URL, headers and raw body can be edited.
- [ ] Choosing another request and returning restores earlier in-memory edits.
- [ ] Two open collection children do not share or overwrite request state.
- [ ] UI still resizes and keyboard navigation remains functional.
- [ ] No fake success state or blocking placeholder network operation.

**Out of scope:** libcurl UI wiring, persistence, importers.

### Slice 3 — Standalone libcurl request engine and local tests

**Outcome:** A tested native C++ HTTP engine can send requests independently of MFC.

**Work:**

1. Define `HttpRequest`/`HttpResponse` value types and a narrow HTTP execution interface without MFC dependencies.
2. Implement method, URL, headers, optional raw UTF-8 bytes/body; collect status, response headers, body and timing.
3. Use libcurl with certificate verification enabled, sensible timeouts, redirects with a finite maximum, and correct handling of 4xx/5xx.
4. Run blocking libcurl calls **only** on a worker or in tests; do not call them from a UI message handler.
5. Write automated integration tests using a **loopback-only** local HTTP server/fixture for GET, POST echo, headers, status codes, timeout, malformed URL, and transport failure.
6. Add response-size safeguards and binary-body classification/documented handling.

**Acceptance:**

- [ ] Tests prove GET and POST transmit the expected URL, method, headers and exact body bytes.
- [ ] A 404 response is returned as an HTTP response, not reported as `curl` transport failure.
- [ ] A connection failure and timeout report useful transport errors.
- [ ] TLS verification remains enabled by default.
- [ ] No test depends on a public API or internet access.
- [ ] `Http`/`Core` can be built and tested without launching the MFC GUI.

**Out of scope:** UI plumbing; advanced authentication; importers.

### Slice 4 — End-to-end Send and response viewer

**Outcome:** EasyPost32 becomes an actually useful HTTP client.

**Work:**

1. Wire the Slice 2 editor to the Slice 3 engine via a background worker and a UI-thread completion message/dispatcher.
2. Show request progress, prevent accidental duplicate sends on the same editor, surface transport errors; implement Cancel if reliable with current worker design.
3. Display response HTTP status, headers, body, elapsed time and transferred bytes. Expose clear distinctions between **HTTP errors** and **transport errors**.
4. Support plain text and JSON bodies, including an unformatted raw view; display a safe placeholder for binary data.
5. Ensure MDI child closure does not lead to use-after-free via late worker callbacks. Keep request/response ownership explicit.
6. Optionally integrate Scintilla for editing/viewing; do not block the slice on syntax highlighting.

**Acceptance:**

- [ ] Send GET/POST/PUT/PATCH/DELETE from the GUI to the local fixture.
- [ ] Editing and sending a JSON body transmits the body without implicit rewrite.
- [ ] The UI remains responsive during a slow request.
- [ ] HTTP 400/500 responses are shown with their bodies and status; transport failures are distinct.
- [ ] Repeated sends, MDI document closure while pending, and ordinary error cases do not crash the app.
- [ ] Status/timing/headers/body are actually visible and readable.

**Out of scope:** collection save/load; import/export.

### Slice 5 — Local collections, variables and history

**Outcome:** Collections are durable local working documents; common workflows no longer require retyping.

**Work:**

1. Finalise and document versioned `.dp32.json` format; add New/Open/Save/Save As for each collection via MFC document commands.
2. Support multiple named requests and nested folders; add basic create/rename/duplicate/delete/reorder where feasible.
3. Implement `{{name}}` substitution from **collection-level variables** for URL, headers and raw body **at send time only**; do not mutate the text displayed or saved.
4. Reject/flag unresolved variables instead of sending literal unresolved templates; define behaviour for duplicate names and nonrecursive expansion. Keep query-parameter encoding explicit; don't indiscriminately URL-encode entire templates.
5. Add unsaved-change detection and confirm when closing a modified child. Attempt atomic document saves (temporary file + replacement) to avoid corruption.
6. Add local, opt-in or easily clearable **metadata-only** history: timestamp, method, sanitised URL, status, elapsed time; never save Authorization/cookie values or request/response bodies by default.
7. For collection secrets, implement secure storage (e.g. Windows Credential Manager) or do not persist the secret values; never silently store marked secrets in plaintext. Document that explicitly authored headers and ordinary variables in exported JSON are visible plaintext.

**Acceptance:**

- [ ] Save a collection with folders, multiple requests and variables; close/reopen with equivalent content and ordering.
- [ ] Open two files simultaneously as independent MDI children; unsaved edits prompt on close.
- [ ] `{{baseUrl}}` resolves for transmission while the stored/displayed request retains its template.
- [ ] Unresolved variables give an actionable error before sending.
- [ ] History remains local and can be cleared; no default token/body leakage.
- [ ] Malformed or unknown-version files fail clearly without damaging existing documents.

**Out of scope:** full environment stacks, sync, advanced secret management, third-party formats.

### Slice 6 — Postman v2.1 and OpenAPI JSON interoperability

**Outcome:** Existing API collections/specifications can be brought into EasyPost32 without rebuilding them manually.

**Work, phase A — Postman:**

1. Import **Postman Collection v2.1 JSON**: collection metadata, nested `item` groups, request names, methods, raw/structured URLs, headers, raw bodies and collection variables.
2. Export the supported internal fields back to valid Postman v2.1 JSON.
3. Produce a human-readable import/export compatibility report for unsupported parts (e.g. scripts, complex auth, special body modes, examples). **Never execute imported scripts.**
4. Add tests for representative real-world fixtures and supported-field round trips.

**Work, phase B — OpenAPI:**

5. Import **OpenAPI 3.0.x and 3.1.x JSON** (not YAML in v0.1). Create requests from `paths` + HTTP operations; use `servers` to select a base URL; populate applicable path/query/header parameters and common JSON request-body examples when provided.
6. Support **local in-document** `#/...` references needed for common parameters, request bodies and schemas; report external/remote references as unsupported without fetching them.
7. Handle absent examples conservatively: show a blank editable body or basic documented placeholder; do not claim complete JSON Schema instance generation.
8. Open imports as local **unsaved** EasyPost32 collection documents until the user chooses Save As.

**Acceptance:**

- [ ] Valid Postman v2.1 fixtures import folders, requests, methods, headers, variables and raw JSON bodies correctly.
- [ ] Exported Postman v2.1 JSON is schema-valid for the supported mapping, and supported fields survive import → export → import.
- [ ] Unsupported material is reported before any potentially lossy export; scripts are not run.
- [ ] OpenAPI 3.0 and 3.1 JSON fixtures produce selectable requests with correct operations and URLs/parameters.
- [ ] A local `$ref` fixture imports correctly; an external `$ref` is clearly reported and is **never** fetched during import.
- [ ] Import works offline, validates bad input and does not damage existing documents.

**Out of scope:** Postman v3 YAML-folder format, Swagger/OpenAPI v2, OpenAPI YAML, full OAS 3.2 support, remote `$ref`, executable Postman scripts.

## 5. Cross-slice quality bar

Every slice is complete only when all of the following are true:

1. Relevant **x64 Debug build succeeds** (and Release when practical); existing features still work.
2. New non-UI logic has tests, with deterministic local fixtures where relevant.
3. The demonstrated behaviour matches its acceptance checks; identify any checks that were not actually verified.
4. A brief changelog or handoff explains what changed, how to run it and what remains for the next slice.
5. No cloud account, telemetry, update service, hidden network check or sign-in requirement has been added.
6. No unnecessary framework, DI container, architecture layer or large third-party runtime is introduced.
7. Any changed source compiles with appropriate warnings addressed, not disabled to mask errors.
8. Any secrets, authentication headers and URLs containing tokens are treated as potentially sensitive, including in logs and screenshots.

### Usability targets (not premature performance guarantees)

- Startup should feel immediate on an ordinary development PC.
- Request execution must never freeze or hang the main window.
- Idle resource usage should remain modest; measure after there is a representative working build instead of inventing a memory budget now.
- Network operation and failure messages should be comprehensible without opening a debugger.

## 6. Design decisions / deliberate deferrals

- **x64-first:** `32` is part of the name and retro aesthetic, not an architectural restriction. Win32 APIs work from x64 applications.
- **Classic MDI first:** If the generated wizard defaults to tabbed MDI, disable it if straightforward. Do not rewrite the framework to chase an aesthetic detail.
- **MFC vs Win32:** MFC supplies application/document plumbing; use native controls where possible. Avoid reimplementing MDI or inventing a custom widget system.
- **Files before database:** Keep collections editable and portable. Use SQLite only if actual history complexity justifies it.
- **No secrets in history:** Prioritise this from the beginning. Plaintext credentials typed directly into ordinary collection headers are a documented risk, not silently promised to be encrypted.
- **No external import fetching:** Postman and OpenAPI imports read a local file only. HTTP requests to user-chosen endpoints are the app's explicit networking function.
- **Standards support is a subset:** Specify precisely what can be imported. Avoid marketing the app as fully compatible with all Postman/OpenAPI features.
- **Dependency and packaging:** Development may restore open-source packages using vcpkg; **runtime usage requires no package manager or internet connection**. A future installer/redistributable plan may include the relevant Visual C++ runtime and any dynamically linked dependencies.

## 7. Setup checklist for the developer

1. On **Windows**, install Visual Studio (Community is sufficient) with **Desktop development with C++** and optional **C++ MFC/ATL** components plus a Windows SDK.
2. Install or configure vcpkg for native C++ dependencies when the first dependency is introduced.
3. Create a new **MFC Application**, select **Multiple Documents (MDI)** and x64; save and commit the unmodified project scaffold before agent changes.
4. Place `SPEC.md` and `AGENTS.md` at the repository root.
5. Run Codex **one slice at a time**. Begin with Slice 1 and give it permission to inspect/build the project locally.
6. Review the running application at the end of each slice before authorising the next.

### Suggested first Codex instruction

```text
Read AGENTS.md and SPEC.md. Implement only Slice 1 from SPEC.md.
Use the Visual Studio MFC MDI project already in the repository, or if the
repository is empty and the wizard cannot be run unattended, explain exactly
what scaffold I should generate. Preserve native MFC/Win32 UI conventions.
Build x64 Debug if the Windows toolchain is available, and report the actual
build result and how I can verify two MDI child documents open independently.
Do not implement networking, persistence or importers yet.
```

## 8. Authoritative technical references

- Microsoft MFC overview: https://learn.microsoft.com/en-us/cpp/mfc/mfc-and-atl?view=msvc-170
- MFC `CMultiDocTemplate`: https://learn.microsoft.com/en-us/cpp/mfc/document-templates-and-the-document-view-creation-process?view=msvc-170
- libcurl interfaces: https://curl.se/libcurl/c/libcurl-multi.html
- libcurl tutorial: https://curl.se/libcurl/c/libcurl-tutorial.html
- Postman v2.1 schema: https://schema.postman.com/json/collection/v2.1.0/docs/index.html
- Postman format versions: https://learning.postman.com/docs/use/use-collections/collections-schemas/
- OpenAPI 3.0.x: https://spec.openapis.org/oas/v3.0.4.html
- OpenAPI 3.1.x: https://spec.openapis.org/oas/v3.1.1.html
- vcpkg manifest mode: https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode
- Scintilla: https://www.scintilla.org/
