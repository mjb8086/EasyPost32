param(
    [string]$Executable = (Join-Path $PSScriptRoot '..\x64\Debug\EasyPost32.exe')
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
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);

    public static string Text(IntPtr window) {
        var text = new StringBuilder(512); GetWindowText(window, text, text.Capacity); return text.ToString();
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
    Assert-Check (@([MdiSmokeNative]::Children($client)).Count -eq 1) 'One empty collection opens at startup'
    Invoke-CommandId 0xE100
    Invoke-CommandId 0xE100
    $children = @([MdiSmokeNative]::Children($client))
    $titles = @($children | ForEach-Object { [MdiSmokeNative]::Text($_) })
    Assert-Check ($children.Count -eq 3 -and @($titles | Select-Object -Unique).Count -eq 3) 'New creates three independent, distinctly titled children'
    Write-Output "Collections: $($titles -join ', ')"

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
