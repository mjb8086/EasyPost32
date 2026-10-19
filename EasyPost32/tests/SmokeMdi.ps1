param(
    [string]$Executable = (Join-Path $PSScriptRoot '..\x64\Debug\EasyPost32.exe'),
    [switch]$Editor,
    [string]$Screenshot
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

function Assert-Check([bool]$Passed, [string]$Description) {
    if (-not $Passed) { throw $Description }
    Write-Output "PASS: $Description"
}
function Invoke-CommandId([int]$Command) {
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

    $menu = [MdiSmokeNative]::GetSubMenu([MdiSmokeNative]::GetMenu($main), 1)
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
    Invoke-CommandId 0xE141
    Assert-Check ($app.WaitForExit(5000) -and $app.ExitCode -eq 0) 'Application exits normally with a child open'
}
finally {
    if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force }
}
