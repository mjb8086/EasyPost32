# EasyPost32 — Slice 3 HTTP engine and local tests

This project uses C++20, MFC document/view and classic MDI child windows.
Each collection has a separate `CCollectionDoc` and `CCollectionFrame`, with a
native splitter between a request tree (`CCollectionView`) and an editor
(`CRequestEditor`). The main frame has native menus, a text toolbar and a status
bar. One collection opens at startup; Ctrl+N creates another.

Each collection starts with two blank requests. Edit GET, POST, PUT, PATCH,
DELETE, HEAD or OPTIONS, a URL, ordered header lines, and a raw multiline body.
Changes are retained when switching requests. Each document owns its own
plain-C++ `RequestDraft` values, with UTF-8 text and explicit conversion at
the Windows UI boundary. Repeated headers and unfinished header lines remain
editable source text; raw bodies are never parsed or reformatted.

**All edits are in memory only. Closing a collection discards its edits.**
Send and Open are disabled. The standalone libcurl engine is implemented and
tested independently of MFC; GUI transmission begins in Slice 4. Collection
files, variables, history and import/export have not been implemented.

See [Http/README.md](Http/README.md) for HTTP behavior, dependency setup, the
single-command integration tests and their loopback-only TLS fixtures.

## Prerequisites

- Windows 10/11, x64.
- Visual Studio 2026 with Desktop development with C++, the v145 toolset,
  a Windows SDK, and **C++ MFC for x64/x86 (Latest MSVC)**.
- The project uses MFC in a shared DLL. Debug builds require the matching
  development runtime supplied by Visual Studio.
- HTTP engine/tests: Visual Studio's bundled vcpkg (or `VCPKG_ROOT`) and Python
  3.13, with dependencies restored once via `tests\SetupHttp.ps1`.

If MSBuild reports `MSB8041`, close Visual Studio and its related processes,
open Visual Studio Installer, choose Modify for the installation, and select
the MFC component above under Individual components. Its component ID is
`Microsoft.VisualStudio.Component.VC.ATLMFC`. ATL alone is insufficient.
Do not force-close an IDE with unsaved work.

## Build and run

From this project directory in a Developer PowerShell for Visual Studio:

```powershell
msbuild .\EasyPost32.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /nologo
msbuild .\EasyPost32.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo
& .\x64\Debug\EasyPost32.exe
```

To build the whole solution, first restore the HTTP dependencies:

```powershell
powershell.exe -NoProfile -File .\tests\SetupHttp.ps1
msbuild .\EasyPost32.slnx /p:Configuration=Debug /p:Platform=x64 /m /nologo
powershell.exe -NoProfile -File .\tests\TestHttp.ps1 -Configuration Debug
```

Normal HTTP test runs use only loopback sockets and work offline after setup.
They build the standalone engine/test target without launching the GUI.

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
   setup or network dependency. The GUI performs no startup requests or HTTP execution.
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
dragging, Tab traversal, F6 pane switching, Enter in the body, and disabled Send.
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
5. Confirm Send stays disabled and the notice explains that sending is
   unavailable. No fabricated response or pending/success state is displayed.
6. Close the collection only when its temporary edits are no longer needed.

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

The existing solution, project identity, icons and About dialog were retained.
The original Win32 entry point was replaced because it supplied no MFC
document/view or MDI implementation to adapt.
