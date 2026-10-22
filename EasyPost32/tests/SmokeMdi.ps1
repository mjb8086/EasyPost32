param(
    [string]$Executable = (Join-Path $PSScriptRoot '..\x64\Debug\EasyPost32.exe'),
    [switch]$Editor,
    [switch]$InteropOnly,
    [string]$Screenshot,
    [string]$HttpBase,
    [string]$TlsBase,
    [string]$RefusedBase,
    [string]$TruncatedBase
)

# Drive the real application through native window/menu messages. No product CLI.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class MdiSmokeNative
{
    public delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string name, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetClassName(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetNextDlgTabItem(IntPtr dialog, IntPtr control, bool previous);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr window, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    public struct GuiThreadInfo {
        public uint Size, Flags;
        public IntPtr Active, Focus, Capture, MenuOwner, MoveSize, Caret;
        public Rect CaretRect;
    }
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread, ref GuiThreadInfo info);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr menu, int index);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr menu, int index);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetMenuString(IntPtr menu, uint item, StringBuilder text, int count, uint flags);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr menu, uint item, uint flags);
    [DllImport("user32.dll", SetLastError = true)]
    static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr wParam,
        IntPtr lParam, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW", CharSet = CharSet.Unicode)]
    static extern IntPtr SendText(IntPtr window, uint message, IntPtr wParam,
        string text, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW", CharSet = CharSet.Unicode)]
    static extern IntPtr ReadText(IntPtr window, uint message, IntPtr wParam,
        StringBuilder text, uint flags, uint timeout, out IntPtr result);
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint process);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint allocation, uint protection);
    [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint type);
    [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr read);
    [StructLayout(LayoutKind.Sequential)]
    struct ListItem {
        public uint Mask; public int Item, SubItem; public uint State, StateMask;
        public IntPtr Text; public int TextLength, Image; public IntPtr Data;
        public int Indent, Group; public uint Columns; public IntPtr ColumnArray, ColumnFormats; public int GroupIndex;
    }
    // Win32 list-view item messages require pointers in the target process, unlike
    // WM_GETTEXT. Allocate only in the private GUI instance under test.
    static byte[] StructureBytes<T>(T value) {
        int size = Marshal.SizeOf(typeof(T)); IntPtr buffer = Marshal.AllocHGlobal(size);
        try { Marshal.StructureToPtr(value, buffer, false); var bytes = new byte[size]; Marshal.Copy(buffer, bytes, 0, size); return bytes; }
        finally { Marshal.FreeHGlobal(buffer); }
    }
    static void RemoteMessage(IntPtr window, Func<IntPtr, byte[]> make, Action<IntPtr, IntPtr> action) {
        uint owner; GetWindowThreadProcessId(window, out owner);
        IntPtr process = OpenProcess(0x38, false, owner);
        if (process == IntPtr.Zero) throw new Exception("Cannot access private GUI control memory");
        IntPtr remote = IntPtr.Zero;
        try {
            remote = VirtualAllocEx(process, IntPtr.Zero, new UIntPtr(65536), 0x3000, 4);
            if (remote == IntPtr.Zero) throw new Exception("Cannot allocate GUI control message");
            byte[] bytes = make(remote); UIntPtr written;
            if (!WriteProcessMemory(process, remote, bytes, new UIntPtr((uint)bytes.Length), out written) || written.ToUInt64() != (ulong)bytes.Length)
                throw new Exception("Cannot write GUI control message");
            action(process, remote);
        } finally { if (remote != IntPtr.Zero) VirtualFreeEx(process, remote, UIntPtr.Zero, 0x8000); CloseHandle(process); }
    }
    public static string ListText(IntPtr list, int row, int column) {
        string text = null;
        RemoteMessage(list, remote => StructureBytes(new ListItem { Item = row, SubItem = column, Text = IntPtr.Add(remote, 256), TextLength = 16000 }),
            (process, remote) => {
                int count = checked((int)Send(list, 0x1073, row, remote.ToInt64()).ToInt64());
                var bytes = new byte[count * 2]; UIntPtr read;
                if (count > 0 && (!ReadProcessMemory(process, IntPtr.Add(remote, 256), bytes, new UIntPtr((uint)bytes.Length), out read) || read.ToUInt64() != (ulong)bytes.Length))
                    throw new Exception("Cannot read GUI table cell");
                text = Encoding.Unicode.GetString(bytes);
            });
        return text;
    }
    public static void SelectListRow(IntPtr list, int row) {
        RemoteMessage(list, remote => StructureBytes(new ListItem { State = 3, StateMask = 3 }),
            (process, remote) => { Send(list, 0x102B, row, remote.ToInt64()); });
    }
    public static void SelectHistoryTab(IntPtr dialog, int tab) {
        IntPtr tabs = Control(dialog, 1213);
        Send(dialog, 0x28, tabs.ToInt64(), 1); // WM_NEXTDLGCTL puts focus on the real tab control.
        for (int attempt = 0; attempt < 8; ++attempt) {
            int current = (int)Send(tabs, 0x130B, 0, 0).ToInt64();
            if (current == tab) return;
            int key = current < tab ? 0x27 : 0x25;
            Send(tabs, 0x100, key, 0); Send(tabs, 0x101, key, 0);
        }
        throw new Exception("History tab keyboard selection failed");
    }

    public static string Text(IntPtr window) {
        var text = new StringBuilder(512); GetWindowText(window, text, text.Capacity); return text.ToString();
    }
    public static string ControlText(IntPtr window) {
        int count = checked((int)Send(window, 0xE, 0, 0).ToInt64() + 1);
        var text = new StringBuilder(count); IntPtr result;
        if (ReadText(window, 0xD, new IntPtr(count), text, 2, 5000, out result) == IntPtr.Zero)
            throw new Exception("Control text read timed out");
        return text.ToString();
    }
    public static void ReplaceText(IntPtr window, string text) {
        Send(window, 0xB1, 0, -1); IntPtr result;
        // EM_REPLACESEL takes the real edit path and emits EN_CHANGE for multiline controls.
        if (SendText(window, 0xC2, new IntPtr(1), text, 2, 5000, out result) == IntPtr.Zero)
            throw new Exception("Control text edit timed out");
    }
    public static IntPtr Control(IntPtr parent, int id) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (window, data) => {
            if (GetDlgCtrlID(window) == id) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr Tree(IntPtr parent) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (window, data) => {
            var name = new StringBuilder(256); GetClassName(window, name, name.Capacity);
            if (name.ToString() == "SysTreeView32") { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr MainWindow(int process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, data) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            var name = new StringBuilder(256); GetClassName(window, name, name.Capacity);
            if (owner == process && name.ToString().StartsWith("Afx:")) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr Dialog(int process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, data) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            var name = new StringBuilder(256); GetClassName(window, name, name.Capacity);
            if (owner == process && name.ToString() == "#32770" && IsWindowVisible(window)) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr FirstClass(IntPtr parent, string className) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (window, data) => {
            var name = new StringBuilder(256); GetClassName(window, name, name.Capacity);
            if (name.ToString() == className) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static string ControlClasses(IntPtr parent) {
        var found = new List<string>();
        EnumChildWindows(parent, (window, data) => {
            var name = new StringBuilder(256); GetClassName(window, name, name.Capacity);
            found.Add(name.ToString() + ":" + GetDlgCtrlID(window)); return true;
        }, IntPtr.Zero);
        return string.Join(", ", found);
    }
    public static string StaticText(IntPtr parent) {
        var found = new List<string>();
        EnumChildWindows(parent, (window, data) => {
            var name = new StringBuilder(256); GetClassName(window, name, name.Capacity);
            if (name.ToString() == "Static") found.Add(ControlText(window));
            return true;
        }, IntPtr.Zero);
        return string.Join(" | ", found);
    }
    public static IntPtr Client(IntPtr main) { return FindWindowEx(main, IntPtr.Zero, "MDIClient", null); }
    public static IntPtr[] Children(IntPtr client) {
        var children = new List<IntPtr>(); IntPtr child = IntPtr.Zero;
        while ((child = FindWindowEx(client, child, null, null)) != IntPtr.Zero)
            if ((GetWindowLongPtr(child, -20).ToInt64() & 0x40) != 0) children.Add(child);
        return children.ToArray();
    }
    public static IntPtr Send(IntPtr window, uint message, long wParam, long lParam) {
        IntPtr result;
        if (SendMessageTimeout(window, message, new IntPtr(wParam), new IntPtr(lParam),
                2, 5000, out result) == IntPtr.Zero) throw new Exception("Window message timed out: " + message);
        return result;
    }
    public static string MenuText(IntPtr menu, int index) {
        var text = new StringBuilder(512); GetMenuString(menu, (uint)index, text, text.Capacity, 0x400);
        return text.ToString();
    }
    public static Rect Bounds(IntPtr window) { Rect rect; GetWindowRect(window, out rect); return rect; }
    public static IntPtr Focus(IntPtr window) {
        uint process; uint thread = GetWindowThreadProcessId(window, out process);
        var info = new GuiThreadInfo(); info.Size = (uint)Marshal.SizeOf(info);
        if (!GetGUIThreadInfo(thread, ref info)) throw new Exception("Cannot query application keyboard focus");
        return info.Focus;
    }
}
'@

$script:checkCount = 0
function Assert-Check([bool]$Passed, [string]$Description) {
    if (-not $Passed) { throw $Description }
    ++$script:checkCount
    Write-Output "PASS: $Description"
}
function Invoke-CommandId([int]$Command) {
    if ($Command -in @(0xE102, 0xE141)) {
        $oldChild = [MdiSmokeNative]::Send([MdiSmokeNative]::Client($script:main), 0x229, 0, 0)
        [MdiSmokeNative]::PostMessage($script:main, 0x111, [IntPtr]$Command, [IntPtr]::Zero) | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            if ($app.HasExited -or ($Command -eq 0xE102 -and -not [MdiSmokeNative]::IsWindow($oldChild))) { return }
            $dialog = [MdiSmokeNative]::Dialog($app.Id)
            if ($dialog -ne [IntPtr]::Zero -and [MdiSmokeNative]::Control($dialog, 7) -ne [IntPtr]::Zero) {
                # Discard only synthetic edits in this private test instance.
                [MdiSmokeNative]::PostMessage($dialog, 0x111, [IntPtr]7, [IntPtr]::Zero) | Out-Null
            }
            Start-Sleep -Milliseconds 20
        } while ([DateTime]::UtcNow -lt $deadline)
        throw 'Synthetic test collection did not close'
    }
    [MdiSmokeNative]::Send($script:main, 0x111, $Command, 0) | Out-Null
}

$app = Start-Process -FilePath (Resolve-Path -LiteralPath $Executable).Path -WindowStyle Hidden -PassThru
try {
    $client = [IntPtr]::Zero
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ($app.HasExited) { throw "Application exited during startup: $($app.ExitCode)" }
        $script:main = [MdiSmokeNative]::MainWindow($app.Id)
        if ($main -ne [IntPtr]::Zero) { $client = [MdiSmokeNative]::Client($main) }
        if ($client -ne [IntPtr]::Zero -and @([MdiSmokeNative]::Children($client)).Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)

    Assert-Check ($main -ne [IntPtr]::Zero -and $client -ne [IntPtr]::Zero) 'Native MDI frame launches'
    Assert-Check ([MdiSmokeNative]::Text($main).Contains('EasyPost32')) 'Application title is EasyPost32'
    Assert-Check (@([MdiSmokeNative]::Children($client)).Count -eq 1) 'One collection opens at startup'
    if ($InteropOnly) {
        . (Join-Path $PSScriptRoot 'InteropChecks.ps1')
        Test-Interop -Main $main -HttpBase $HttpBase -WorkDirectory $env:EASYPOST_TEST_DIRECTORY -Screenshot $Screenshot
        Invoke-CommandId 0xE141
        Assert-Check ($app.WaitForExit(5000) -and $app.ExitCode -eq 0) 'Application exits normally after interoperability checks'
        Write-Output "$script:checkCount GUI checks passed"
        return
    }
    Invoke-CommandId 0xE100
    Invoke-CommandId 0xE100
    $children = @([MdiSmokeNative]::Children($client))
    $titles = @($children | ForEach-Object { [MdiSmokeNative]::Text($_) })
    Assert-Check ($children.Count -eq 3 -and @($titles | Select-Object -Unique).Count -eq 3) 'New creates three independent, distinctly titled children'
    Write-Output "Collections: $($titles -join ', ')"

    if ($Editor) {
        . (Join-Path $PSScriptRoot 'EditorChecks.ps1')
        Test-RequestEditor -Children $children -Main $main -Screenshot $Screenshot
    }

    $mainMenu = [MdiSmokeNative]::GetMenu($main)
    $menu = [IntPtr]::Zero
    for ($i = 0; $i -lt [MdiSmokeNative]::GetMenuItemCount($mainMenu); ++$i) {
        if ([MdiSmokeNative]::MenuText($mainMenu, $i).Replace('&', '') -eq 'Window') {
            $menu = [MdiSmokeNative]::GetSubMenu($mainMenu, $i)
        }
    }
    $entries = @(for ($i = 0; $i -lt [MdiSmokeNative]::GetMenuItemCount($menu); ++$i) {
        $id = [MdiSmokeNative]::GetMenuItemID($menu, $i)
        if ($id -ge 0xFF00 -and $id -lt 0xFF10) { $id }
    })
    Assert-Check ($entries.Count -eq 3) 'Window menu lists all three collections'
    foreach ($id in $entries) {
        Invoke-CommandId $id
        $active = [MdiSmokeNative]::Send($client, 0x229, 0, 0)
        Assert-Check ([MdiSmokeNative]::GetDlgCtrlID($active) -eq $id) "Window menu activates child $id"
    }

    Invoke-CommandId 0xE132
    $rects = @($children | ForEach-Object { [MdiSmokeNative]::Bounds($_) })
    Assert-Check (@($rects | ForEach-Object { $_.Top } | Select-Object -Unique).Count -gt 1) 'Cascade offsets child windows'
    Invoke-CommandId 0xE133
    $rects = @($children | ForEach-Object { [MdiSmokeNative]::Bounds($_) })
    Assert-Check (@($rects | ForEach-Object { $_.Top } | Select-Object -Unique).Count -eq 3) 'Tile horizontally separates children'
    Invoke-CommandId 0xE134
    $rects = @($children | ForEach-Object { [MdiSmokeNative]::Bounds($_) })
    Assert-Check (@($rects | ForEach-Object { $_.Left } | Select-Object -Unique).Count -eq 3) 'Tile vertically separates children'

    $closing = [MdiSmokeNative]::Send($client, 0x229, 0, 0)
    Invoke-CommandId 0xE102
    $remaining = @([MdiSmokeNative]::Children($client))
    Assert-Check (-not [MdiSmokeNative]::IsWindow($closing) -and $remaining.Count -eq 2 -and -not $app.HasExited) 'Closing one child preserves the application and other two children'
    Invoke-CommandId 0xE102
    Invoke-CommandId 0xE102
    Assert-Check (@([MdiSmokeNative]::Children($client)).Count -eq 0 -and -not $app.HasExited) 'Closing all children preserves the main frame'
    Invoke-CommandId 0xE100
    Assert-Check (@([MdiSmokeNative]::Children($client)).Count -eq 1) 'New works after the last child closes'
    if ($HttpBase) {
        . (Join-Path $PSScriptRoot 'HttpGuiChecks.ps1')
        Test-HttpGui -Main $main -HttpBase $HttpBase -TlsBase $TlsBase -RefusedBase $RefusedBase -TruncatedBase $TruncatedBase -Screenshot $Screenshot
        . (Join-Path $PSScriptRoot 'CollectionChecks.ps1')
        Test-Collections -Main $main -HttpBase $HttpBase -WorkDirectory $env:EASYPOST_TEST_DIRECTORY -Screenshot $Screenshot
        . (Join-Path $PSScriptRoot 'InteropChecks.ps1')
        Test-Interop -Main $main -HttpBase $HttpBase -WorkDirectory $env:EASYPOST_TEST_DIRECTORY -Screenshot $Screenshot
    }
    Invoke-CommandId 0xE141
    Assert-Check ($app.WaitForExit(5000) -and $app.ExitCode -eq 0) 'Application exits normally with a child open'
    Write-Output "$script:checkCount GUI checks passed"
}
finally {
    if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force }
}
