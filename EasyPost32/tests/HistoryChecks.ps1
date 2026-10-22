# Called with the real modal history dialog after a recorded synthetic POST.
function Test-HistoryDetails([IntPtr]$Dialog, [string]$ExpectedBody, [string]$Screenshot) {
    $table = [MdiSmokeNative]::Control($Dialog, 1212)
    $details = [MdiSmokeNative]::Control($Dialog, 1214)
    $resend = [MdiSmokeNative]::Control($Dialog, 1217)
    $header = [MdiSmokeNative]::Send($table, 0x101F, 0, 0)
    Assert-Check ([MdiSmokeNative]::FirstClass($Dialog, 'SysListView32') -eq $table -and [MdiSmokeNative]::Send($header, 0x1200, 0, 0).ToInt64() -eq 6) 'History uses a native report table with six columns'
    Assert-Check ([MdiSmokeNative]::Send($table, 0x1004, 0, 0).ToInt64() -eq 1 -and [MdiSmokeNative]::ListText($table, 0, 5) -ceq 'This session') 'Recorded history row has session details'
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($Dialog, 1215)).EndsWith('/inspect?token=synthetic-query')) 'Selected history displays the actual sent URL rather than a later edit'
    [MdiSmokeNative]::SelectHistoryTab($Dialog, 0)
    $requestHeaders = [MdiSmokeNative]::ControlText($details)
    Assert-Check ($requestHeaders.Contains('Authorization: Bearer synthetic-gui-secret') -and $requestHeaders.Contains("X-Repeat: one`r`nX-Repeat: two")) 'Request header tab retains resolved credentials and repeated headers in session memory'
    [MdiSmokeNative]::SelectHistoryTab($Dialog, 1)
    Assert-Check ([MdiSmokeNative]::ControlText($details) -ceq $ExpectedBody) 'Request body tab shows the exact original sent snapshot'
    [MdiSmokeNative]::SelectHistoryTab($Dialog, 2)
    Assert-Check ([MdiSmokeNative]::ControlText($details).Contains('x-reply: one')) 'Response header tab displays the captured response headers'
    [MdiSmokeNative]::SelectHistoryTab($Dialog, 3)
    $originalResponse = [MdiSmokeNative]::ControlText($details)
    $originalWire = $originalResponse | ConvertFrom-Json
    Assert-Check ($originalWire.method -ceq 'POST' -and $originalWire.body_hex.Length -gt 0) 'Response body tab displays the captured response data'
    # Automatic recording may be off; an explicit Resend still appends that one result.
    [MdiSmokeNative]::Send([MdiSmokeNative]::Control($Dialog, 1209), 0xF5, 0, 0) | Out-Null
    [MdiSmokeNative]::Send($resend, 0xF5, 0, 0) | Out-Null
    Assert-Check (-not [MdiSmokeNative]::IsWindowEnabled($resend)) 'History resend runs in the background and suppresses duplicate sends'
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (([MdiSmokeNative]::Send($table, 0x1004, 0, 0).ToInt64() -ne 2 -or -not [MdiSmokeNative]::IsWindowEnabled($resend)) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
    Assert-Check ([MdiSmokeNative]::Send($table, 0x1004, 0, 0).ToInt64() -eq 2 -and [MdiSmokeNative]::ListText($table, 0, 3) -ceq 'HTTP 200') 'Resend appends a new completed result at the top of the table'
    Assert-Check ([MdiSmokeNative]::Send($table, 0x102C, 0, 3).ToInt64() -eq 3) 'The new top history result is selected automatically'
    $wire = [MdiSmokeNative]::ControlText($details) | ConvertFrom-Json
    Assert-Check ($wire.body_hex -ceq $originalWire.body_hex -and $wire.query -ceq $originalWire.query -and $wire.method -ceq $originalWire.method) 'History resend transmits the original body, method and query despite collection edits'
    Assert-Check (@($wire.headers | Where-Object { $_[0] -ceq 'authorization' -and $_[1] -ceq 'Bearer synthetic-gui-secret' }).Count -eq 1) 'History resend retains the original resolved header snapshot'
    Assert-Check ([MdiSmokeNative]::Send([MdiSmokeNative]::Control($Dialog, 1209), 0xF0, 0, 0).ToInt64() -eq 0) 'Explicit resend does not enable automatic recording'
    [MdiSmokeNative]::SelectListRow($table, 1)
    Assert-Check ([MdiSmokeNative]::ControlText($details) -ceq $originalResponse) 'Clicking the older row restores its original response without overwriting it'
    $historyPath = Join-Path $env:LOCALAPPDATA 'EasyPost32\history.json'
    $lockedHistory = [IO.File]::Open($historyPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        [MdiSmokeNative]::Send($resend, 0xF5, 0, 0) | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while (-not [MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($Dialog, 1219)).Contains('save failed') -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
        Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($Dialog, 1219)).Contains('save failed') -and [MdiSmokeNative]::Send($table, 0x1004, 0, 0).ToInt64() -eq 2) 'A failed history replacement preserves the original rows and reports the save failure'
        $unsavedWire = [MdiSmokeNative]::ControlText($details) | ConvertFrom-Json
        Assert-Check ($unsavedWire.body_hex -ceq $originalWire.body_hex -and [MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($Dialog, 1216)).Contains('HTTP 200')) 'The actual new response remains inspectable when saving resend history fails'
    } finally { $lockedHistory.Dispose() }
    [MdiSmokeNative]::SelectListRow($table, 1)
    if ($Screenshot) {
        Add-Type -AssemblyName System.Drawing
        [MdiSmokeNative]::ShowWindow($Dialog, 5) | Out-Null
        [MdiSmokeNative]::UpdateWindow($Dialog) | Out-Null
        $rect = [MdiSmokeNative]::Bounds($Dialog)
        $bitmap = New-Object Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $dc = $graphics.GetHdc()
        try { if (-not [MdiSmokeNative]::PrintWindow($Dialog, $dc, 2)) { throw 'History capture failed' } }
        finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
        $path = [IO.Path]::Combine([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Screenshot)), [IO.Path]::GetFileNameWithoutExtension($Screenshot) + '-history.png')
        try { $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png) }
        finally { $bitmap.Dispose() }
    }
}
