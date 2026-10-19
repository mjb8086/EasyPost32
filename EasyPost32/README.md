# EasyPost32 — Slice 1 scaffold

This project uses C++20, MFC document/view and classic MDI child windows.
Each new collection gets a separate `CCollectionDoc`, `CCollectionFrame` and
`CCollectionView`. The main frame has native menus, a text toolbar and a status
bar. One empty collection opens at startup; Ctrl+N creates another.

Only Slice 1 is implemented. Open is disabled, and collection save/load,
request editing, networking and import/export are not implemented yet.

## Prerequisites

- Windows 10/11, x64.
- Visual Studio 2026 with Desktop development with C++, the v145 toolset,
  a Windows SDK, and **C++ MFC for x64/x86 (Latest MSVC)**.
- The project uses MFC in a shared DLL. Debug builds require the matching
  development runtime supplied by Visual Studio.

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
   setup or network dependency. This slice contains no HTTP implementation.
7. Exit normally, including with multiple empty children still open.

## Native MDI smoke check

`tests\SmokeMdi.ps1` starts a separate application instance and exercises its
real MDI windows through Windows messages. It checks startup, New Collection,
unique child titles, Window menu activation, Cascade/Tile, closing one or all
children, creating another child, and normal exit. The script cleans up only
the instance it starts. It requires Windows desktop access and does not add a
command-line interface to the application.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\SmokeMdi.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\SmokeMdi.ps1 -Executable .\x64\Release\EasyPost32.exe
```

The execution-policy override applies only to the launched PowerShell process;
it does not change the machine or user execution policy. Codex can retain a
scoped command approval for this script to avoid repeated desktop-access prompts.

These checks supplement the visual/debugger checks above; they do not verify
text legibility, display scaling, keyboard input, or launch under Visual Studio.

## Verification recorded during implementation

- The original Win32 project built successfully with x64 Debug before changes.
- After MFC was installed, the MFC scaffold built successfully as x64 Debug
  and x64 Release with MSBuild, v145 and C++20. No compilation fixes were
  needed. The Release build required execution outside the sandbox because
  MSBuild's file tracker encountered an access-denied error inside it.
- `git diff --check` and the smoke script's PowerShell syntax check passed.
- The native MDI smoke check passed all 15 assertions against both x64 Debug
  and x64 Release (both commands above exited with code 0). It verified startup,
  three distinctly titled collection children, Window menu activation, Cascade,
  both Tile commands, independent closure, keeping the main frame open after
  closing every child, creating another child, and normal application exit.
- The smoke check runs a hidden application instance. Visual legibility,
  keyboard interaction, display scaling, disconnected-network launch and
  launch under the Visual Studio debugger remain unverified. The startup code
  contains no HTTP implementation, sign-in or online setup. Slice 1's build
  and automated MDI behavior checks pass; the remaining manual acceptance
  checks are listed above.
- Transport tests and the FastAPI fixture begin in Slice 3.

The existing solution, project identity, icons and About dialog were retained.
The original Win32 entry point was replaced because it supplied no MFC
document/view or MDI implementation to adapt.
