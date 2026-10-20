# Exercise only real native file/report dialogs and editable imported documents.
function Test-Interop([IntPtr]$Main, [string]$HttpBase, [string]$WorkDirectory, [string]$Screenshot) {
    $client = [MdiSmokeNative]::Client($Main)
    $survivor = [MdiSmokeNative]::Send($client, 0x229, 0, 0)
    $originalBody = [MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($survivor, 1004))
    $initialCount = @([MdiSmokeNative]::Children($client)).Count

    function Wait-InteropDialog {
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $dialog = [MdiSmokeNative]::Dialog($app.Id)
            if ($dialog -ne [IntPtr]::Zero) { return $dialog }
            Start-Sleep -Milliseconds 25
        } while ([DateTime]::UtcNow -lt $deadline)
        throw 'Interop native dialog did not appear'
    }
    function Start-InteropDialog([int]$Command) {
        [MdiSmokeNative]::PostMessage($Main, 0x111, [IntPtr]$Command, [IntPtr]::Zero) | Out-Null
        return Wait-InteropDialog
    }
    function End-InteropDialog([IntPtr]$Dialog, [int]$Button = 1) {
        [MdiSmokeNative]::PostMessage($Dialog, 0x111, [IntPtr]$Button, [IntPtr]::Zero) | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([MdiSmokeNative]::IsWindow($Dialog) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
        if ([MdiSmokeNative]::IsWindow($Dialog)) { throw 'Interop dialog did not close' }
    }
    function Select-InteropFile([IntPtr]$Dialog, [string]$Path) {
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $combo = [MdiSmokeNative]::Control($Dialog, 1148)
            $edit = if ($combo -ne [IntPtr]::Zero) { [MdiSmokeNative]::FirstClass($combo, 'Edit') } else { [IntPtr]::Zero }
            if ($edit -eq [IntPtr]::Zero) { $edit = [MdiSmokeNative]::Control($Dialog, 1152) }
            if ($edit -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 25
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($edit -eq [IntPtr]::Zero) { throw 'Interop file-name edit unavailable' }
        [MdiSmokeNative]::ReplaceText($edit, $Path)
        [MdiSmokeNative]::PostMessage($Dialog, 0x111, [IntPtr]1, [MdiSmokeNative]::Control($Dialog, 1)) | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([MdiSmokeNative]::IsWindow($Dialog) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
        if ([MdiSmokeNative]::IsWindow($Dialog)) { throw 'Interop file chooser did not complete' }
    }
    function Import-Interop([int]$Command, [string]$Path) {
        Select-InteropFile (Start-InteropDialog $Command) $Path
        return Wait-InteropDialog
    }
    function Accept-Import([IntPtr]$Report, [int]$ExpectedCount) {
        End-InteropDialog $Report
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        while (@([MdiSmokeNative]::Children($client)).Count -ne $ExpectedCount -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
        Assert-Check (@([MdiSmokeNative]::Children($client)).Count -eq $ExpectedCount) 'Continue on import creates one independent MDI child' | Out-Host
        return [MdiSmokeNative]::Send($client, 0x229, 0, 0)
    }
    function Select-ImportedRequest([IntPtr]$Child, [int]$Index) {
        $tree = [MdiSmokeNative]::Tree($Child)
        $root = [MdiSmokeNative]::Send($tree, 0x110A, 0, 0)
        $item = [MdiSmokeNative]::Send($tree, 0x110A, 4, $root.ToInt64())
        for ($position = 0; $position -lt $Index; ++$position) { $item = [MdiSmokeNative]::Send($tree, 0x110A, 1, $item.ToInt64()) }
        [MdiSmokeNative]::Send($tree, 0x110B, 9, $item.ToInt64()) | Out-Null
    }
    function Wait-ImportedResponse([IntPtr]$Child) {
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 25
            $text = [MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($Child, 1007))
            if ($text -match 'HTTP 200' -and [MdiSmokeNative]::IsWindowEnabled([MdiSmokeNative]::Control($Child, 1002))) { return }
        } while ([DateTime]::UtcNow -lt $deadline)
        throw ('Imported GUI request failed to complete with HTTP 200: ' + $text + ' | method index: ' + [MdiSmokeNative]::Send([MdiSmokeNative]::Control($Child, 1000), 0x147, 0, 0).ToInt64())
    }
    $source = Join-Path $WorkDirectory 'import-source.json'
    $fixture = Get-Content (Join-Path $PSScriptRoot 'Fixtures\postman-v21.json') -Raw | ConvertFrom-Json
    $fixture.variable[0].value = $HttpBase
    [IO.File]::WriteAllText($source, ($fixture | ConvertTo-Json -Depth 30))
    $sourceBefore = [IO.File]::ReadAllText($source)
    $report = Import-Interop 32784 $source
    Assert-Check ([MdiSmokeNative]::Control($report, 1211) -ne [IntPtr]::Zero) 'Postman import shows a readable compatibility report before creating a document'
    End-InteropDialog $report 2
    Assert-Check (@([MdiSmokeNative]::Children($client)).Count -eq $initialCount) 'Cancel import leaves all existing documents unchanged'
    $report = Import-Interop 32784 $source
    $child = Accept-Import $report ($initialCount + 1)
    Assert-Check ([MdiSmokeNative]::Send([MdiSmokeNative]::Tree($child), 0x1105, 0, 0).ToInt64() -eq 5) 'Postman import rebuilds the tree with both nested folders and requests'
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($child, 1004)) -ceq $fixture.item[0].item[0].item[0].request.body.raw) 'Nested Postman request is selectable with exact raw JSON source'
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($survivor, 1004)) -ceq $originalBody) 'Import preserves the existing collection editor'
    $save = Start-InteropDialog 0xE103
    Assert-Check ([MdiSmokeNative]::Control($save, 1148) -ne [IntPtr]::Zero) 'Save on an imported unsaved document opens a native destination chooser'
    End-InteropDialog $save 2
    Invoke-CommandId 32771
    Wait-ImportedResponse $child
    $wire = [MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($child, 1009)) | ConvertFrom-Json
    $expectedHex = ([BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($fixture.item[0].item[0].item[0].request.body.raw))).Replace('-', '').ToLowerInvariant()
    Assert-Check ($wire.method -ceq 'POST' -and $wire.body_hex -ceq $expectedHex -and $wire.query -ceq 'one=1&one=2') 'Imported Postman GUI Send transmits exact UTF-8 body and query bytes'
    $destination = Join-Path $WorkDirectory 'gui-export.postman.json'
    $exportReport = Start-InteropDialog 32786
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($exportReport, 1211)).Contains('Only mapped')) 'Export presents compatibility warnings before choosing a destination'
    End-InteropDialog $exportReport 2
    Assert-Check (-not (Test-Path -LiteralPath $destination)) 'Cancel export writes no destination file'
    End-InteropDialog (Start-InteropDialog 32786)
    Select-InteropFile (Wait-InteropDialog) $destination
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (-not (Test-Path -LiteralPath $destination) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
    Assert-Check (([IO.File]::ReadAllText($destination) | ConvertFrom-Json).item[0].item[0].item[0].request.body.raw -ceq $fixture.item[0].item[0].item[0].request.body.raw) 'Native export writes supported Postman fields without rewriting the body'
    Assert-Check ([IO.File]::ReadAllText($source) -ceq $sourceBefore) 'Import, Send and Export never overwrite the source collection'
    Invoke-CommandId 0xE102

    $unsupported = Join-Path $PSScriptRoot 'Fixtures\postman-unsupported.json'
    $report = Import-Interop 32784 $unsupported
    $notes = [MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($report, 1211))
    Assert-Check ($notes.Contains('scripts/tests') -and $notes.Contains('authentication') -and $notes.Contains('Non-raw')) 'Postman scripts, auth helpers and special body modes are explicitly reported'
    $child = Accept-Import $report ($initialCount + 1)
    $native = Join-Path $WorkDirectory 'imported.dp32.json'
    Select-InteropFile (Start-InteropDialog 0xE104) $native
    Invoke-CommandId 0xE102
    Select-InteropFile (Start-InteropDialog 0xE101) $native
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (@([MdiSmokeNative]::Children($client)).Count -ne ($initialCount + 1) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
    $report = Start-InteropDialog 32786
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($report, 1211)).Contains('scripts/tests')) 'Saved/reopened native imports retain loss warnings before later export'
    End-InteropDialog $report 2
    Invoke-CommandId 0xE102

    foreach ($version in @('30', '31')) {
        $source = Join-Path $WorkDirectory "openapi-$version.json"
        $fixture = Get-Content (Join-Path $PSScriptRoot "Fixtures\openapi-$version.json") -Raw | ConvertFrom-Json
        $fixture.servers = @(@{url = $HttpBase})
        if ($version -eq '31') { $fixture.paths.'/inspect'.put.servers = @(@{url = $HttpBase}) }
        [IO.File]::WriteAllText($source, ($fixture | ConvertTo-Json -Depth 30))
        $report = Import-Interop 32785 $source
        $child = Accept-Import $report ($initialCount + 1)
        Assert-Check ([MdiSmokeNative]::Send([MdiSmokeNative]::Tree($child), 0x1105, 0, 0).ToInt64() -eq 4) "OpenAPI $version tree contains its three imported operations"
        Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($child, 1001)).StartsWith('{{baseUrl}}/inspect')) "OpenAPI $version creates selectable requests with URL templates"
        if ($version -eq '30') {
            Select-ImportedRequest $child 2
            Assert-Check ([MdiSmokeNative]::Send([MdiSmokeNative]::Control($child, 1000), 0x147, 0, 0).ToInt64() -eq 1) 'OpenAPI POST request can be selected independently of GET and DELETE'
        }
        Invoke-CommandId 32771
        Wait-ImportedResponse $child
        $wire = [MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($child, 1009)) | ConvertFrom-Json
        Assert-Check ($wire.method -ceq $(if ($version -eq '30') {'POST'} else {'PUT'}) -and $wire.body_hex.Length -gt 0) "OpenAPI $version imported JSON example sends successfully from the GUI"
        if ($Screenshot -and $version -eq '31') {
            Add-Type -AssemblyName System.Drawing
            [MdiSmokeNative]::ShowWindow($Main, 5) | Out-Null
            [MdiSmokeNative]::Send($client, 0x225, $child.ToInt64(), 0) | Out-Null
            [MdiSmokeNative]::UpdateWindow($Main) | Out-Null
            $rect = [MdiSmokeNative]::Bounds($Main)
            $bitmap = New-Object System.Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $dc = $graphics.GetHdc()
            try { if (-not [MdiSmokeNative]::PrintWindow($Main, $dc, 2)) { throw 'Interop capture failed' } }
            finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
            try { $bitmap.Save([IO.Path]::GetFullPath($Screenshot), [System.Drawing.Imaging.ImageFormat]::Png) }
            finally { $bitmap.Dispose() }
            [MdiSmokeNative]::ShowWindow($Main, 0) | Out-Null
        }
        Invoke-CommandId 0xE102
    }
    $bad = Join-Path $WorkDirectory 'bad-import.json'
    foreach ($command in @(32784, 32785)) {
        [IO.File]::WriteAllText($bad, '{')
        $errorDialog = Import-Interop $command $bad
        Assert-Check ([MdiSmokeNative]::StaticText($errorDialog) -match 'Invalid Postman|Invalid OpenAPI|Invalid JSON') 'Malformed imports report their format error clearly'
        $button = if ([MdiSmokeNative]::Control($errorDialog, 1) -ne [IntPtr]::Zero) {1} else {2}
        End-InteropDialog $errorDialog $button
        Assert-Check (@([MdiSmokeNative]::Children($client)).Count -eq $initialCount -and [MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($survivor, 1004)) -ceq $originalBody) 'Failed import leaves existing documents intact'
    }
}
