# Real native dialogs and document commands; no application test hooks or CLI.
function Test-Collections([IntPtr]$Main, [string]$HttpBase, [string]$WorkDirectory, [string]$Screenshot) {
    if (-not $WorkDirectory -or -not (Test-Path -LiteralPath $WorkDirectory)) { throw 'Private fixture directory is required' }
    $client = [MdiSmokeNative]::Client($Main)
    Invoke-CommandId 0xE102 # Close the pending Slice 4 synthetic document.
    Invoke-CommandId 0xE100
    $childA = [MdiSmokeNative]::Send($client, 0x229, 0, 0)
    $tree = [MdiSmokeNative]::Tree($childA)
    $url = [MdiSmokeNative]::Control($childA, 1001)
    $body = [MdiSmokeNative]::Control($childA, 1004)
    $headers = [MdiSmokeNative]::Control($childA, 1003)
    $summary = [MdiSmokeNative]::Control($childA, 1007)
    $responseBody = [MdiSmokeNative]::Control($childA, 1009)

    function Wait-Dialog {
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $dialog = [MdiSmokeNative]::Dialog($app.Id)
            if ($dialog -ne [IntPtr]::Zero) { return $dialog }
            Start-Sleep -Milliseconds 25
        } while ([DateTime]::UtcNow -lt $deadline)
        throw 'Expected native dialog did not appear'
    }
    function Close-Dialog([IntPtr]$Dialog, [int]$Button = 1) {
        [MdiSmokeNative]::PostMessage($Dialog, 0x111, [IntPtr]$Button, [IntPtr]::Zero) | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([MdiSmokeNative]::IsWindow($Dialog) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
        if ([MdiSmokeNative]::IsWindow($Dialog)) {
            throw ('Native dialog did not close: ' + [MdiSmokeNative]::Text($Dialog) + ' | ' + [MdiSmokeNative]::StaticText($Dialog) + ' | ' + [MdiSmokeNative]::ControlClasses($Dialog))
        }
    }
    function Open-Dialog([int]$Command) {
        [MdiSmokeNative]::PostMessage($Main, 0x111, [IntPtr]$Command, [IntPtr]::Zero) | Out-Null
        return Wait-Dialog
    }
    function Name-Item([int]$Command, [string]$Name) {
        $dialog = Open-Dialog $Command
        [MdiSmokeNative]::ReplaceText([MdiSmokeNative]::Control($dialog, 1200), $Name)
        Close-Dialog $dialog
    }
    function File-Dialog([int]$Command, [string]$Path, [switch]$ExpectError) {
        $oldCount = @([MdiSmokeNative]::Children($client)).Count
        $dialog = Open-Dialog $Command
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $hostControl = [MdiSmokeNative]::Control($dialog, 1148) # cmb13, native Explorer file name combo
            $fileEdit = if ($hostControl -ne [IntPtr]::Zero) { [MdiSmokeNative]::FirstClass($hostControl, 'Edit') } else { [IntPtr]::Zero }
            if ($fileEdit -eq [IntPtr]::Zero) { $fileEdit = [MdiSmokeNative]::Control($dialog, 1152) }
            if ($fileEdit -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 25
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($fileEdit -eq [IntPtr]::Zero) { throw ('Native file-name edit was not found: ' + [MdiSmokeNative]::ControlClasses($dialog)) }
        [MdiSmokeNative]::ReplaceText($fileEdit, $Path)
        if ([MdiSmokeNative]::ControlText($fileEdit) -cne $Path) { throw 'File-name edit did not retain the selected path' }
        $accept = [MdiSmokeNative]::Control($dialog, 1)
        [MdiSmokeNative]::PostMessage($dialog, 0x111, [IntPtr]1, $accept) | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 25
            $next = [MdiSmokeNative]::Dialog($app.Id)
            if ($ExpectError -and $next -ne [IntPtr]::Zero -and $next -ne $dialog) { return $next }
            if (-not $ExpectError -and -not [MdiSmokeNative]::IsWindow($dialog)) {
                if ($Command -eq 0xE104 -and (Test-Path -LiteralPath $Path)) { return }
                if ($Command -eq 0xE101 -and @([MdiSmokeNative]::Children($client)).Count -gt $oldCount) { return }
            }
        } while ([DateTime]::UtcNow -lt $deadline)
        $activeDialog = [MdiSmokeNative]::Dialog($app.Id)
        $files = @(Get-ChildItem -LiteralPath $WorkDirectory -File | ForEach-Object { $_.Name })
        throw ('File dialog did not complete: ' + [MdiSmokeNative]::Text($activeDialog) + ' | ' + [MdiSmokeNative]::StaticText($activeDialog) + ' | files: ' + ($files -join ', '))
    }
    function Select-Tree([IntPtr]$Item) { [MdiSmokeNative]::Send($tree, 0x110B, 9, $Item.ToInt64()) | Out-Null }
    function Tree-Next([IntPtr]$Item, [int]$Relation) { return [MdiSmokeNative]::Send($tree, 0x110A, $Relation, $Item.ToInt64()) }
    function Wait-Response([IntPtr]$Child) {
        $status = [MdiSmokeNative]::Control($Child, 1007)
        $send = [MdiSmokeNative]::Control($Child, 1002)
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 25
            if ([MdiSmokeNative]::IsWindowEnabled($send) -and [MdiSmokeNative]::ControlText($status) -notmatch '^(Sending|Cancelling|Ready)') { return }
        } while ([DateTime]::UtcNow -lt $deadline)
        throw 'Collection request did not complete'
    }

    Name-Item 32776 'Folder A'
    Name-Item 32776 'Nested A'
    Name-Item 32775 'Echo raw'
    $method = [MdiSmokeNative]::Control($childA, 1000)
    [MdiSmokeNative]::Send($method, 0x14E, 1, 0) | Out-Null
    [MdiSmokeNative]::Send([MdiSmokeNative]::GetParent($method), 0x111, (0x10000 -bor 1000), $method.ToInt64()) | Out-Null
    $template = "{`r`n`t`"message`" : `"{{message}}`"`r`n}`r`n  "
    [MdiSmokeNative]::ReplaceText($url, '{{baseUrl}}/inspect?token=synthetic-query')
    [MdiSmokeNative]::ReplaceText($headers, "Authorization: Bearer {{token}}`r`nX-Repeat: one`r`nX-Repeat: two")
    [MdiSmokeNative]::ReplaceText($body, $template)
    $selected = Tree-Next ([IntPtr]::Zero) 9
    Select-Tree (Tree-Next $selected 3) # Nested A folder
    Invoke-CommandId 32778 # Duplicate folder and renew all descendant IDs.
    Invoke-CommandId 32780 # Move copy above original.
    Name-Item 32777 'Nested B'
    $root = Tree-Next ([IntPtr]::Zero) 0
    $first = Tree-Next $root 4
    $second = Tree-Next $first 1
    Select-Tree $second
    $confirm = Open-Dialog 32779
    Close-Dialog $confirm 6
    $root = Tree-Next ([IntPtr]::Zero) 0
    $folder = Tree-Next (Tree-Next $root 4) 1
    $nestedB = Tree-Next $folder 4
    $nestedA = Tree-Next $nestedB 1
    Select-Tree (Tree-Next $nestedA 4)
    Assert-Check ([MdiSmokeNative]::ControlText($body) -ceq $template) 'Nested request editing survives folder duplication, rename, reorder and deletion'

    $variables = Open-Dialog 32782
    foreach ($row in @(@('baseUrl', $HttpBase, $false), @('message', 'synthetic-message', $false), @('token', 'synthetic-gui-secret', $true))) {
        [MdiSmokeNative]::Send([MdiSmokeNative]::Control($variables, 1207), 0xF5, 0, 0) | Out-Null
        [MdiSmokeNative]::ReplaceText([MdiSmokeNative]::Control($variables, 1202), $row[0])
        [MdiSmokeNative]::ReplaceText([MdiSmokeNative]::Control($variables, 1203), $row[1])
        if ($row[2]) { [MdiSmokeNative]::Send([MdiSmokeNative]::Control($variables, 1204), 0xF5, 0, 0) | Out-Null }
        [MdiSmokeNative]::Send([MdiSmokeNative]::Control($variables, 1205), 0xF5, 0, 0) | Out-Null
    }
    Close-Dialog $variables
    $fileA = Join-Path $WorkDirectory 'collection-a.dp32.json'
    $fileB = Join-Path $WorkDirectory 'collection-b.dp32.json'
    File-Dialog 0xE104 $fileA # Save As
    $savedText = [IO.File]::ReadAllText($fileA)
    $saved = $savedText | ConvertFrom-Json
    Assert-Check ($saved.format -eq 'EasyPost32.collection' -and $saved.version -eq 1 -and $saved.items.Count -eq 2) 'Save As writes a versioned native collection through the real file dialog'
    Assert-Check ($saved.items[1].items[0].name -ceq 'Nested B' -and $saved.items[1].items[1].name -ceq 'Nested A' -and $saved.items[1].items[1].items[0].request.body -ceq $template) 'Saved folders, requests, sibling order and exact raw body match the editor'
    Assert-Check (-not $savedText.Contains('synthetic-gui-secret') -and $saved.variables[2].secret -and -not ($saved.variables[2].PSObject.Properties.Name -contains 'value')) 'Marked secret value is omitted from saved JSON'
    $historyPath = Join-Path $env:LOCALAPPDATA 'EasyPost32\history.json'
    Assert-Check (-not (Test-Path -LiteralPath $historyPath)) 'Request history creates no file before explicit opt-in'
    $history = Open-Dialog 32783
    Assert-Check ([MdiSmokeNative]::Send([MdiSmokeNative]::Control($history, 1209), 0xF0, 0, 0).ToInt64() -eq 0) 'History recording is disabled by default'
    [MdiSmokeNative]::Send([MdiSmokeNative]::Control($history, 1209), 0xF5, 0, 0) | Out-Null
    Close-Dialog $history
    Invoke-CommandId 32771
    Wait-Response $childA
    $result = [MdiSmokeNative]::ControlText($responseBody) | ConvertFrom-Json
    $expectedBody = $template.Replace('{{message}}', 'synthetic-message')
    $expectedHex = [BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($expectedBody)).Replace('-', '').ToLowerInvariant()
    Assert-Check ($result.body_hex -ceq $expectedHex -and $result.query -ceq 'token=synthetic-query') 'GUI variables resolve in the actual transmitted URL and raw body'
    $auth = @($result.headers | Where-Object { $_[0] -ceq 'authorization' } | ForEach-Object { $_[1] })
    Assert-Check ($auth.Count -eq 1 -and $auth[0] -ceq 'Bearer synthetic-gui-secret') 'Secret header substitution uses the in-memory session value'
    Assert-Check ([MdiSmokeNative]::ControlText($url).StartsWith('{{baseUrl}}') -and [MdiSmokeNative]::ControlText($body) -ceq $template -and [IO.File]::ReadAllText($fileA) -ceq $savedText) 'Sending leaves displayed and saved templates unchanged'
    if ($Screenshot) {
        Add-Type -AssemblyName System.Drawing
        [MdiSmokeNative]::Send($client, 0x225, $childA.ToInt64(), 0) | Out-Null
        [MdiSmokeNative]::UpdateWindow($Main) | Out-Null
        $rect = [MdiSmokeNative]::Bounds($Main)
        $bitmap = New-Object System.Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $dc = $graphics.GetHdc()
        try { if (-not [MdiSmokeNative]::PrintWindow($Main, $dc, 2)) { throw 'Window capture failed' } }
        finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
        try { $bitmap.Save([IO.Path]::GetFullPath($Screenshot), [Drawing.Imaging.ImageFormat]::Png) }
        finally { $bitmap.Dispose() }
    }
    $historyText = [IO.File]::ReadAllText($historyPath)
    $historyRows = ($historyText | ConvertFrom-Json).entries
    Assert-Check ($historyRows.Count -eq 1 -and $historyRows[0].method -ceq 'POST' -and $historyRows[0].url -ceq '[endpoint omitted]' -and $historyRows[0].status -eq 200 -and @($historyRows[0].PSObject.Properties).Count -eq 6) 'Opt-in history records only six metadata fields and omits secret-bearing endpoints'
    Assert-Check (-not $historyText.Contains('synthetic-gui-secret') -and -not $historyText.Contains('synthetic-query') -and -not $historyText.Contains('Authorization') -and -not $historyText.Contains('synthetic-message')) 'History file contains no credentials, query token, headers or raw body'
    $history = Open-Dialog 32783
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($history, 1208)).Contains('POST')) 'History viewer displays completed request metadata'
    [MdiSmokeNative]::Send([MdiSmokeNative]::Control($history, 1210), 0xF5, 0, 0) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($history, 1208)) -eq '' -and (([IO.File]::ReadAllText($historyPath) | ConvertFrom-Json).entries.Count -eq 0)) 'Clear history removes entries from both the viewer and local file'
    [MdiSmokeNative]::Send([MdiSmokeNative]::Control($history, 1209), 0xF5, 0, 0) | Out-Null
    Close-Dialog $history
    [MdiSmokeNative]::ReplaceText($url, '{{missing}}/inspect')
    Invoke-CommandId 32771
    Assert-Check ([MdiSmokeNative]::ControlText($summary).Contains('Unresolved variable: missing')) 'Unresolved GUI variable gives an actionable error before starting a worker'
    [MdiSmokeNative]::ReplaceText($url, '{{baseUrl}}/inspect?token=synthetic-query')
    Invoke-CommandId 0xE103 # Save existing path.
    File-Dialog 0xE104 $fileB
    File-Dialog 0xE101 $fileA # Open alongside file B.
    $childB = [MdiSmokeNative]::Send($client, 0x229, 0, 0)
    Assert-Check ($childB -ne $childA -and @([MdiSmokeNative]::Children($client)).Count -eq 2) 'Two saved collection files open as independent MDI documents'
    $treeB = [MdiSmokeNative]::Tree($childB)
    $rootB = [MdiSmokeNative]::Send($treeB, 0x110A, 0, 0)
    $folderB = [MdiSmokeNative]::Send($treeB, 0x110A, 4, $rootB.ToInt64())
    $folderB = [MdiSmokeNative]::Send($treeB, 0x110A, 1, $folderB.ToInt64())
    $nested = [MdiSmokeNative]::Send($treeB, 0x110A, 4, $folderB.ToInt64())
    $request = [MdiSmokeNative]::Send($treeB, 0x110A, 4, $nested.ToInt64())
    [MdiSmokeNative]::Send($treeB, 0x110B, 9, $request.ToInt64()) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($childB, 1004)) -ceq $template) 'Reopening restores nested request source without formatting'
    Invoke-CommandId 32771
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($childB, 1007)).Contains('secret variable needs a session value')) 'Reopened secret variables require re-entry instead of silently sending an empty credential'
    [MdiSmokeNative]::ReplaceText([MdiSmokeNative]::Control($childB, 1004), 'independent edited body')
    [MdiSmokeNative]::Send($client, 0x222, $childA.ToInt64(), 0) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText($body) -ceq $template) 'Editing one saved document does not overwrite another document'
    [MdiSmokeNative]::Send($client, 0x222, $childB.ToInt64(), 0) | Out-Null
    $prompt = Open-Dialog 0xE102
    Assert-Check ([MdiSmokeNative]::Control($prompt, 6) -ne [IntPtr]::Zero -and [MdiSmokeNative]::Control($prompt, 7) -ne [IntPtr]::Zero) 'Closing a dirty document prompts to save changes'
    Close-Dialog $prompt 2
    Assert-Check ([MdiSmokeNative]::IsWindow($childB)) 'Cancel on the unsaved-change prompt retains the document'
    $prompt = Open-Dialog 0xE102
    Close-Dialog $prompt 6
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([MdiSmokeNative]::IsWindow($childB) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
    $closedSaved = [IO.File]::ReadAllText($fileA) | ConvertFrom-Json
    Assert-Check (-not [MdiSmokeNative]::IsWindow($childB) -and $closedSaved.items[1].items[0].items[0].request.body -ceq 'independent edited body') 'Save on close persists changes before closing the document'
    Assert-Check (([IO.File]::ReadAllText($fileB) | ConvertFrom-Json).items[1].items[1].items[0].request.body -ceq $template) 'Saving the other document preserves the original file content'
    foreach ($badContent in @('{', '{"format":"EasyPost32.collection","version":99}')) {
        $badFile = Join-Path $WorkDirectory 'invalid.dp32.json'
        [IO.File]::WriteAllText($badFile, $badContent)
        $errorDialog = File-Dialog 0xE101 $badFile -ExpectError
        Assert-Check ([MdiSmokeNative]::StaticText($errorDialog) -match 'Invalid collection JSON|Unsupported collection version') 'Invalid-file dialog reports the actual JSON or version problem'
        $button = if ([MdiSmokeNative]::Control($errorDialog, 1) -ne [IntPtr]::Zero) { 1 } else { 2 }
        Close-Dialog $errorDialog $button
        # The failed document's frame is destroyed after the modal error returns.
        $deadline = [DateTime]::UtcNow.AddSeconds(3)
        while (@([MdiSmokeNative]::Children($client)).Count -ne 1 -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 25
        }
        Assert-Check (@([MdiSmokeNative]::Children($client)).Count -eq 1 -and [MdiSmokeNative]::ControlText($body) -ceq $template) 'Malformed or unknown-version collection fails without damaging the open document'
    }
}
