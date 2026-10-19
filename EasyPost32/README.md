# EasyPost32 — Slice 4 Send and response viewer

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

**All edits are in memory only. Closing a collection discards its edits.**
Send snapshots the current request and runs libcurl on a worker. The response
pane displays HTTP status, elapsed milliseconds, received/sent body bytes,
redirect count, final response headers and the unformatted UTF-8 body. Binary
or invalid UTF-8 content gets a safe placeholder. HTTP 4xx/5xx responses retain
their normal status and body; transport failures and partial responses are
identified separately. No request or response content is logged or persisted.

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

Open remains disabled. Collection files, variables, history and import/export
have not been implemented. The application has no product CLI or local server.

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
6. Close the collection only when its temporary edits are no longer needed.

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

The final verification commands, run from this directory, were:

```powershell
& $msbuild .\EasyPost32.slnx /p:Configuration=Debug /p:Platform=x64 /m /nologo /verbosity:minimal
& $msbuild .\EasyPost32.slnx /p:Configuration=Release /p:Platform=x64 /m /nologo /verbosity:minimal
powershell.exe -NoProfile -File .\tests\TestHttp.ps1 -Configuration Debug -SkipBuild
powershell.exe -NoProfile -File .\tests\TestHttp.ps1 -Configuration Release -SkipBuild
powershell.exe -NoProfile -File .\tests\TestGuiHttp.ps1 -Configuration Debug -SkipBuild -Screenshot .\build\slice4-debug.png
powershell.exe -NoProfile -File .\tests\TestGuiHttp.ps1 -Configuration Release -SkipBuild -Screenshot .\build\slice4-release.png
git diff --check
```

The existing solution, project identity, icons and About dialog were retained.
The original Win32 entry point was replaced because it supplied no MFC
document/view or MDI implementation to adapt.
