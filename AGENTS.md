# AGENTS.md — EasyPost32

These are repository-level instructions for Codex and other coding agents. Read **`SPEC.md`** before doing implementation work. The product specification governs requirements and the **six sequential development slices**.

## Mission

Build **EasyPost32**, a small, fast, classic-feeling **native Windows HTTP client**. It is for developers who want to edit collections, send HTTP requests and inspect responses **without an account, cloud workspace, Electron runtime or nagging sign-in UI**.

The name is intentionally retro. **Do not interpret “32” as a requirement to compile 32-bit**: build x64 first and use Win32/MFC APIs.

## Immutable product principles

1. **Native:** C++20, MFC document/view + Win32 controls, classic multiple-document interface (MDI). One collection = one independent MDI child/document.
2. **Offline-first:** No sign-in, telemetry, tracking, cloud service, sync, update check or outbound connection on startup. Network access occurs **only** to endpoints explicitly requested by the user. Development-time dependency restoration is allowed.
3. **Small and useful:** No Electron, CEF, WebView2 dependency, React, .NET UI, or custom browser-based frontend. Prefer existing Windows common controls and straightforward C++.
4. **Real HTTP:** libcurl is the HTTP implementation. Keep it isolated from MFC and off the UI thread. Preserve the exact raw body the user writes.
5. **Local files:** Human-readable, versioned collection documents; do not assume a database or user account.
6. **Honest compatibility:** Support the documented subset of Postman v2.1 JSON and OpenAPI 3.0/3.1 JSON. Never promise or fake universal Postman/OpenAPI compatibility.
7. **Safety:** Do not disable TLS validation. Do not leak headers, credentials, secret variable values, or body content into logs/history by default. Do not execute imported scripts or fetch external `$ref` content during import.

## Work in six slices — one at a time

Implement the slice the user requests from `SPEC.md`, in sequence:

1. **Native MFC MDI scaffold** — build and open independent empty collection child windows.
2. **In-memory request editor** — method, URL, headers, raw body and request selection; no pretending to send.
3. **libcurl core + local integration tests** — transport independent of MFC, loopback tests, TLS/timeout/error handling.
4. **GUI Send + response viewer** — background execution, response metadata/body and crash-safe completion.
5. **Collections + variables + history** — versioned local files, per-collection `{{variables}}`, metadata-only history.
6. **Interop** — Postman Collection v2.1 JSON import/export, then OpenAPI 3.0/3.1 JSON import with clear limitations.

**Do not implement future slices early** merely because they seem easy or related. You may establish tiny supporting interfaces as needed, but avoid speculative functionality.

## Working method

- First **inspect the current repository**, its project files, and the state of the last completed slice. Prefer adapting the working MFC wizard scaffold to replacing it.
- Identify the requested slice and list the relevant acceptance criteria before implementing. If no slice was specified, work on the first *incomplete* slice; do not start a second one in the same run.
- Make changes that are **small, understandable, reviewable, and compileable**. Avoid sweeping refactors.
- Build and test with the available Windows/MSVC toolchain. **Never claim a build or test passed without running it.** If you are on a non-Windows host or lack MFC/MSVC, explain the limitation and provide exact Windows verification steps.
- Keep the application runnable at the end of each slice; never replace real functionality with empty placeholders to make tests pass.
- Verify relevant acceptance checks from `SPEC.md`. For UI tasks, explain the manual steps required to validate behaviour.
- At completion report: **files changed**, **build/test commands and actual results**, **acceptance checks passed/not verified**, and **known limitations**. Stop at the slice boundary.

## C++ and MFC conventions

- Use standard C++20 value types, RAII, smart pointers and explicit ownership where appropriate. Prefer simplicity over clever metaprogramming.
- Keep HTTP, parsing, persistence and variable substitution in plain C++ libraries/classes independent of `CWnd`, `CString`, resource IDs and message maps.
- Use MFC document/view for collections (`CMultiDocTemplate`/`CDocument`/view). Use native Windows controls and resource files; don't hand-roll a UI framework.
- Never touch GUI controls from an HTTP worker thread. Marshal results safely to the owning UI thread; ensure closed document windows cannot receive dangling callbacks.
- Don't share one libcurl easy handle concurrently between threads. Set reasonable timeouts, report transport errors separately from HTTP error statuses and preserve TLS verification.
- Avoid heavyweight architectural ceremony: no dependency-injection framework, service locator, unnecessary interfaces, factory pyramids, or multiple representations of the same model.
- Use UTF-8 for persisted JSON/text and explicit Unicode conversion at the Windows UI boundary.
- Don't weaken compiler warnings or comment out failing tests to achieve a green build.

## Dependencies and files

- Prefer **libcurl**, **nlohmann/json**, and, when useful, **Scintilla/Lexilla**. Use a checked-in `vcpkg.json` manifest for package dependencies where possible; document if a component is managed differently.
- **Do not** commit generated binaries, build directories, machine-specific absolute paths, authentication tokens, passwords or real API collections containing secrets.
- The native collection document format will be `.dp32.json`, versioned and tested once Slice 5 begins.
- Use local loopback fixtures for networking tests. Tests must not require a live Postman service, login, SaaS endpoint or outside internet connection.
- Preserve existing user changes and source files. Never silently discard work or overwrite generated Visual Studio resources without inspecting them.

## Security and interoperability safeguards

- Imported Postman scripts are **data only**: never evaluate them.
- OpenAPI import must not resolve remote or external references over the network; clearly report unsupported references.
- Log/history entries must not include Authorization, cookies, secret values or full raw bodies by default. Beware tokens embedded in URL query strings.
- Marked secret variables must not be persisted unprotected. Ordinary custom headers/variables may be plaintext in manually saved collections; document this honestly and warn when appropriate.
- Do not claim that unsupported fields survive an import/export round trip unless tests prove it; report lossy mappings before export.

## What not to do

- No unsolicited feature expansion (OAuth browser workflows, GraphQL UI, automatic documentation generators, cloud collaboration, local server, test scripting, auto-update mechanisms).
- No redesign into Qt, Flutter, Avalonia, WPF or web frontend without the user's express approval.
- No complicated startup dependency that makes the app useless offline.
- No fake HTTP response or mock data presented as a real request result.
- No entire-project rewrite because a narrow feature is awkward to implement.
- No implementation across multiple slices in one task unless the user explicitly requests it.

## Done means

The requested slice **builds (where the correct toolchain is available), works, meets its stated acceptance checks, has focused tests where appropriate, and includes a short honest verification summary**. When uncertain, ask for or document a narrowly scoped decision; do not quietly invent a major new product requirement.

## First implementation request

When starting from a fresh repository, work on **Slice 1 only**. The user may need to create and commit an MFC MDI App Wizard skeleton first. Inspect it, confirm x64 Debug builds, set the application name, and verify multiple independent MDI child windows. Then stop and report results.
