# EasyPost32 — Slice 5 collections, variables and history

This project uses C++20, MFC document/view and classic MDI child windows.
Each collection has a separate `CCollectionDoc` and `CCollectionFrame`, with a
native splitter between a request tree (`CCollectionView`) and an editor
(`CRequestEditor`), plus a second splitter above the response (`CResponseView`).
The main frame has native menus, a text toolbar and a status
bar. One collection opens at startup; Ctrl+N creates another.

Each collection starts with two blank requests. Edit GET, POST, PUT, PATCH,
DELETE, HEAD or OPTIONS, a URL, ordered header lines, and a raw multiline body.
Changes are retained when switching requests. Each document owns its own
plain-C++ `RequestDraft` values, with UTF-8 text and explicit conversion at
the Windows UI boundary. Repeated headers and unfinished header lines remain
editable source text; raw bodies are never parsed or reformatted.

**Collections can now be saved as versioned `.dp32.json` files.** File > Open,
Save and Save As use native Windows file dialogs. Modified documents prompt to
save on close. Nested folders and requests retain their names, order, method,
URL/header/body source and TLS setting across reopen. Responses are never saved
in collection files.
Send snapshots the current request and runs libcurl on a worker. The response
pane displays HTTP status, elapsed milliseconds, received/sent body bytes,
redirect count, final response headers and the unformatted UTF-8 body. Binary
or invalid UTF-8 content gets a safe placeholder. HTTP 4xx/5xx responses retain
their normal status and body; transport failures and partial responses are
identified separately. Request/response bodies and credentials are never history
fields; only explicitly saved collection source is persisted.

Use Send or **Request > Send (Ctrl+Enter)**; **Cancel (Esc)** interrupts a pending
transfer. Each request has its own pending/result state; repeated Send is blocked
until that request completes. Other requests and collections remain usable.
Edits made while sending affect the next Send; the displayed response belongs
to the last Send. Tab/Shift+Tab traverse tree, editor and response controls;
F6/Shift+F6 switch those three panes.

**Verify TLS certificate and hostname** is checked by default for each request.
Unchecking it is an explicit override, shown by a warning in the editor and on
the resulting response. This setting is locked during its pending transfer;
re-enabling it does not hide the warning on an earlier unverified response.
Timeouts remain 5 seconds to connect and 30 seconds total. Closing a collection
cancels its jobs without waiting on the UI thread. Exiting cancels and joins
remaining workers before libcurl cleanup; OS DNS/TLS calls can briefly delay
process exit before observing cancellation.

The Request menu can create folders/requests, rename the selected item (F2),
duplicate subtrees, delete items, and move siblings up/down. Select a folder to
create children inside it, or select the root to create top-level items. Rename
the root to change the collection's stored name. Cross-folder moves are deferred.

Request > Collection Variables edits case-sensitive `{{name}}` placeholders.
They resolve only in a Send snapshot of the URL, headers and raw body. Missing
names, duplicate definitions, malformed placeholders, nested substitutions and
empty required secrets fail before starting HTTP work. No automatic URL encoding
or JSON formatting occurs. **Marked secret values stay in memory and are omitted
on save; reopening requires re-entry.** Ordinary variables, headers and bodies
are saved as plaintext, as disclosed in the editor and Variables dialog.

File > Local History offers session-only opt-in recording, initially off, with
a viewer and Clear button. It stores at most 500 metadata entries under
`%LOCALAPPDATA%\EasyPost32\history.json`. Paths, queries, fragments, user info,
headers and bodies are omitted. Secret-bearing collections omit the entire
endpoint. Recording must be enabled both at Send and at completion.
Closing a pending collection cancels its work without retaining its response in
history. See [the native format and privacy contract](docs/CollectionFormat.md).

Import/export is reserved for Slice 6. The application has no product CLI,
server, sign-in, telemetry or startup network request.

See [Http/README.md](Http/README.md) for HTTP behavior, dependency setup, the
single-command integration tests and their loopback-only TLS fixtures.

## Prerequisites

- Windows 10/11, x64.
- Visual Studio 2026 with Desktop development with C++, the v145 toolset,
  a Windows SDK, and **C++ MFC for x64/x86 (Latest MSVC)**.
- The project uses MFC in a shared DLL. Debug builds require the matching
  development runtime supplied by Visual Studio.
- Build: Visual Studio's bundled vcpkg (or `VCPKG_ROOT`) and restored native
  dependencies. Tests also require Python 3.13. `tests\SetupHttp.ps1` restores
  both once. Running the built application does not require Python.

If MSBuild reports `MSB8041`, close Visual Studio and its related processes,
open Visual Studio Installer, choose Modify for the installation, and select
the MFC component above under Individual components. Its component ID is
`Microsoft.VisualStudio.Component.VC.ATLMFC`. ATL alone is insufficient.
Do not force-close an IDE with unsaved work.

## Build and run

Restore dependencies once before building the GUI or solution. From this
project directory in a Developer PowerShell for Visual Studio:

```powershell
powershell.exe -NoProfile -File .\tests\SetupHttp.ps1
msbuild .\EasyPost32.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /nologo
msbuild .\EasyPost32.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo
& .\x64\Debug\EasyPost32.exe
```

To build the whole solution and run standalone HTTP tests:

```powershell
msbuild .\EasyPost32.slnx /p:Configuration=Debug /p:Platform=x64 /m /nologo
powershell.exe -NoProfile -File .\tests\TestHttp.ps1 -Configuration Debug
```

Normal HTTP test runs use only loopback sockets and work offline after setup.
They build the standalone engine/test target without launching the GUI.
The GUI build references `Http.lib` and deploys matching curl/zlib DLLs next to
the executable. Only x64 builds are supported by the HTTP dependency setup.

Outside a Developer PowerShell, locate MSBuild from the installed Visual Studio:

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild .\EasyPost32.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /nologo /verbosity:minimal
```

Build products go into `x64\Debug` or `x64\Release`; intermediate
files go into `build\<platform>\<configuration>` and are ignored by Git.

For debugging, open `EasyPost32.slnx`, select **Debug / x64**, build, and press
F5. A successful build must precede launch: any executable left from the
original Win32 project is not verification of the MFC scaffold.

## Manual Slice 1 acceptance checks

1. Launch under Visual Studio. Check the EasyPost32 title, menu, toolbar,
   status bar, and first empty Collection child.
2. Use File > New Collection or Ctrl+N twice. Confirm separate collection
   titles and independently movable/resizable child windows.
3. Use Window > Cascade and both Tile commands. Select each collection
   through the Window menu and confirm it becomes active. Check Ctrl+F6
   cycles children using the normal MDI keyboard behavior.
4. Close a child with its close button or Ctrl+F4. Confirm the other children
   and main application remain open. Close all children, then create a new one.
5. Resize the application and inspect the menu, toolbar, status bar and child
   text. Check keyboard menu access, and repeat at the intended display scale.
6. Launch while disconnected from the network. Confirm no sign-in, online
   setup or network dependency. The GUI performs no startup HTTP requests.
7. Exit normally, including with multiple empty children still open.

## Native MDI smoke check

`tests\SmokeMdi.ps1` starts a separate application instance and exercises its
real MDI windows through Windows messages. It checks startup, New Collection,
unique child titles, Window menu activation, Cascade/Tile, closing one or all
children, creating another child, and normal exit. The script cleans up only
the instance it starts. It requires Windows desktop access and does not add a
command-line interface to the application.

```powershell
powershell.exe -NoProfile -File .\tests\SmokeMdi.ps1
powershell.exe -NoProfile -File .\tests\SmokeMdi.ps1 -Editor
powershell.exe -NoProfile -File .\tests\SmokeMdi.ps1 -Editor -Executable .\x64\Release\EasyPost32.exe
```

`-Editor` also loads `tests\EditorChecks.ps1`. It verifies all editor fields,
exact Unicode/whitespace round trips, repeated and unfinished headers, bodies
larger than 32 KiB, independent requests/documents, native resizing and splitter
dragging, Tab traversal, F6 pane switching, Enter in the body, and Send disabled
for an empty URL. It does not send any request without the HTTP fixture options.
The test briefly displays its own application instance to exercise focus.

An optional `-Screenshot .\build\editor.png` captures only that application
window with synthetic inputs. Create the output directory first if needed.
These tests use Windows PowerShell's configured script policy. Codex can retain
a scoped command approval for this script to avoid repeated desktop prompts.

## Manual Slice 2 checks

1. Select Request 1, choose POST, and enter a URL, repeated headers on separate
   lines, and a body containing JSON, Unicode, blank lines and trailing spaces.
2. Select Request 2 and edit it differently. Switch back and forth; confirm
   each method, URL, header source and body is restored without formatting.
3. Create a second collection with Ctrl+N. Confirm its requests start blank
   and editing them does not overwrite the first collection.
4. Drag the splitter, resize or maximize the child, and check that fields remain
   accessible. Small windows scroll the form instead of shrinking fields away.
   Check Tab/Shift+Tab through tree, method, URL, headers and body, F6/Shift+F6
   between panes, and Enter inserting newlines in the raw body.
5. Confirm Send is disabled for an empty URL and enabled after entering one.
   Before the first Send, the response pane says Ready and contains no response.
6. Save the collection or choose Discard on the normal unsaved-change prompt.

## Slice 4 GUI integration and manual checks

```powershell
powershell.exe -NoProfile -File .\tests\TestGuiHttp.ps1 -Configuration Debug
powershell.exe -NoProfile -File .\tests\TestGuiHttp.ps1 -Configuration Release
# Optional window-only screenshot using synthetic fixture data:
powershell.exe -NoProfile -File .\tests\TestGuiHttp.ps1 -SkipBuild -Screenshot .\build\response.png
# Leave fixtures running for manual testing until this GUI instance exits:
powershell.exe -NoProfile -File .\tests\TestGuiHttp.ps1 -SkipBuild -Manual
```

The automatic command builds, starts private HTTP/TLS fixtures on ephemeral
loopback ports, drives a separate GUI instance using native Windows messages,
and cleans up that instance, servers and temporary certificates on failure or
success. It includes the MDI/editor regression suite. Desktop access is needed.
There is no test-time dependency restoration, external endpoint or trust-store
change. Manual mode prints the addresses and launches the ordinary GUI without
application command-line arguments.

For manual acceptance, use the printed HTTP address as `<base>`:

1. Maximize the collection child. Send GET `<base>/inspect?literal=%2F+a`;
   inspect status, timing, bytes, raw headers and JSON body. Send POST, PUT,
   PATCH and DELETE to `<base>/inspect` with `Content-Type: application/json`
   and a body containing Unicode, CR/LF, tabs, blank lines and trailing spaces.
   The returned `body_hex` must match the exact UTF-8 input bytes. POST to
   `<base>/echo` to compare the raw returned body directly.
2. Send GET `<base>/status/400` and `/status/500`; status and body should remain
   visible without a transport error. Try the printed refused address and a
   malformed URL; these must show a distinct transport error.
3. Send GET `<base>/delay/3000`. During Sending, edit text, switch requests,
   create/activate another collection and try Send again. The original request
   must execute once, retain its snapshot and update the correct request.
   Repeat with Cancel/Esc, then close a pending child and keep using another.
4. Send GET `<base>/binary`; confirm the safe placeholder. TLS `/ready` using
   the printed untrusted TLS address must fail by default. Explicitly uncheck
   Verify TLS and retry; inspect the editor and response warnings. Re-enable it.
5. Drag both splitters, resize, and inspect response legibility. Check Ctrl+Enter,
   Esc, Tab/Shift+Tab and F6/Shift+F6. Close the application while a request is
   pending and confirm normal exit.

The native tests also cover timeout, response/header limits, redirects and TLS
hostname/trust validation. Arbitrary DPI changes, assistive technology and live
public services still require separate manual verification.

## Slice 5 collection acceptance checks

The same `TestHttp.ps1` commands now include persistence, variable and history
tests in a separate MFC-free `Core` library. `TestGuiHttp.ps1` additionally
drives the real Save/Open dialogs, nested item commands, Variables and History
dialogs, and unsaved-change prompts. Synthetic collection files and overridden
`LOCALAPPDATA` live only inside the runner's temporary fixture directory; tests
do not touch the developer's normal history.

For manual testing, run `tests\TestGuiHttp.ps1 -SkipBuild -Manual`, then:

1. Open [tests/Fixtures/loopback.dp32.json](tests/Fixtures/loopback.dp32.json).
   Set `baseUrl` in Collection Variables to the printed HTTP address. Select
   Echo raw JSON and Send; the displayed/stored template must retain its braces
   and whitespace while the response contains the resolved `message`.
2. Create nested folders and named requests; duplicate a folder, rename it and
   move siblings. Save As to a new `.dp32.json`, close and reopen it. Confirm
   nesting, ordering, raw source, ordinary variables and TLS settings survive.
3. Save As to a second file and open the first alongside it. Edit one, close it
   and exercise Cancel, Save and Discard. The other document must stay unchanged.
4. Add a marked secret variable, reference it in a header, and Send. Save and
   inspect the JSON: the secret row has no value. Reopen and Send again; it must
   request a session value. Missing `{{names}}` must give actionable errors.
5. Open Local History, explicitly enable recording, then send to fixture URLs
   with synthetic query tokens. Check the viewer/file for metadata only. A
   marked secret anywhere in a collection suppresses its entire endpoint.
   Clear History and confirm the file's entries array is empty.
6. Try malformed JSON or an unsupported version in a separate test file. The
   error must leave other open documents intact. A locked destination must fail
   Save without replacing its previous complete contents.

The on-disk schema, limits, variable rules, atomic-save behavior and history
fields are documented in [docs/CollectionFormat.md](docs/CollectionFormat.md).

## Verification recorded during implementation

- The original Win32 project built successfully with x64 Debug before changes.
- After MFC was installed, the MFC scaffold built successfully as x64 Debug
  and x64 Release with MSBuild, v145 and C++20. No compilation fixes were
  needed. The Release build required execution outside the sandbox because
  MSBuild's file tracker encountered an access-denied error inside it.
- `git diff --check` and the smoke script's PowerShell syntax check passed.
- The Slice 1 smoke checks passed against both configurations. They remain
  included in the extended editor checks for MDI behavior regressions.
- Slice 2 x64 Debug and Release builds passed with MSBuild/v145/C++20, with
  no reported compiler warnings or errors. Both `-Editor` commands above
  passed all 46 assertions and exited with code 0. No external service or
  networking fixture was used. All five Slice 2 acceptance checks passed:
  editable fields, request round trips, independent documents, usable resizing
  and keyboard traversal, and unavailable Send without fabricated responses.
- The Debug window screenshot was inspected at the current display scale:
  tree, labels, URL, headers and raw body were readable and fit the maximized
  child. Arbitrary DPI/monitor changes, screen-reader behavior, reverse keyboard
  traversal and Visual Studio debugger launch have not been manually verified.
- Independent collection editing was also manually confirmed by the developer.
- Slice 3 adds the independent transport and the FastAPI/Uvicorn fixture,
  retaining the existing in-memory editor and disabled Send control.
- Slice 3 native Debug and Release integration suites passed all 29 tests in
  each configuration. The full x64 solution built successfully in both modes,
  and the Debug editor/MDI regression suite passed all 46 checks. The native
  projects compile at warning level 4 without reported warnings. Fixture
  processes and temporary certificate directories were cleaned up after both
  successful and failed test runs. `git diff --check` passed.
- All six Slice 3 acceptance checks passed: exact GET/POST transmission,
  ordinary HTTP error responses, useful refusal/timeout errors, default TLS
  validation, loopback-only tests without test-time dependency restoration,
  and a standalone MFC-free engine/test build. The public internet was used
  only for dependency setup; no live public API or SaaS was used by tests.

- Slice 4 full x64 Debug and Release solution builds passed with MSVC v145 and
  C++20, with no reported compiler warnings or errors. Both configurations
  passed all **35 native tests** and **98 GUI checks**, with exit code 0.
  GUI integration passed for
  exact GET/POST/PUT/PATCH/DELETE, JSON/raw response text, ordered headers,
  HTTP 400/500, malformed URLs/headers, refusal, TLS defaults/explicit override,
  binary and partial responses, snapshots, concurrent request selection,
  duplicate suppression, cancellation, pending document closure, splitters and
  readable response controls. Cancel's Escape accelerator also passed. Both
  response screenshots were inspected; the final splitter preserves the user's
  chosen proportions across child resizing. All six Slice 4 acceptance checks
  passed. PowerShell/Python syntax checks and `git diff --check` passed.
  Test GUI instances and temporary fixture/certificate directories were cleaned
  up. Ctrl+Enter, reverse traversal, arbitrary DPI/monitor changes, accessibility
  and Visual Studio debugger launch have not been manually exercised.

- Slice 5 full x64 Debug and Release solution builds passed with MSVC v145,
  C++20 and no reported compiler warnings/errors. Both configurations passed
  **53 native tests** and **124 GUI checks**, each with exit code 0. All six
  Slice 5 acceptance checks passed: durable ordered/nested collections,
  independent saved documents and close prompts, send-only substitution,
  actionable unresolved-variable errors, clearable metadata-only history, and
  safe rejection of malformed/unknown-version files. Secret omission and
  re-entry were checked in both native and GUI tests. The native suite also
  verifies failed atomic replacement retains the original file.
- Debug collection/response screenshots were inspected at the current display
  scale. The native Save As workflow overrides MFC's default failure cleanup,
  which would otherwise delete a destination after a failed save. Draft edits
  update only the selected tree label, avoiding repeated whole-model searches
  for every node in a large collection. Deleted subtrees cancel their jobs and
  release their response state. Test history lives in an isolated child
  `LOCALAPPDATA`; the developer's normal history is not modified.
- PowerShell/Python syntax and whitespace checks passed. Temporary fixtures,
  test instances and diagnostic files were cleaned up after verification.
  Arbitrary DPI/monitor changes, assistive technology, manual Ctrl+Enter/
  reverse navigation and Visual Studio debugger launch remain unverified.

The final Slice 5 verification commands, run from this directory, were:

```powershell
& $msbuild .\EasyPost32.slnx /p:Configuration=Debug /p:Platform=x64 /m /nologo /verbosity:minimal
& $msbuild .\EasyPost32.slnx /p:Configuration=Release /p:Platform=x64 /m /nologo /verbosity:minimal
powershell.exe -NoProfile -File .\tests\TestHttp.ps1 -Configuration Debug -SkipBuild
powershell.exe -NoProfile -File .\tests\TestHttp.ps1 -Configuration Release -SkipBuild
powershell.exe -NoProfile -File .\tests\TestGuiHttp.ps1 -Configuration Debug -SkipBuild -Screenshot .\build\slice5-debug.png
powershell.exe -NoProfile -File .\tests\TestGuiHttp.ps1 -Configuration Release -SkipBuild -Screenshot .\build\slice5-release.png
git diff --check
```

The existing solution, project identity, icons and About dialog were retained.
The original Win32 entry point was replaced because it supplied no MFC
document/view or MDI implementation to adapt.
