# Loaded by SmokeMdi.ps1 -Editor; all input is synthetic and no requests are sent.
function Test-RequestEditor($Children, [IntPtr]$Main, [string]$Screenshot) {
    $first = $Children[0]
    $other = $Children[1]
    $tree = [MdiSmokeNative]::Tree($first)
    $method = [MdiSmokeNative]::Control($first, 1000)
    $url = [MdiSmokeNative]::Control($first, 1001)
    $send = [MdiSmokeNative]::Control($first, 1002)
    $headers = [MdiSmokeNative]::Control($first, 1003)
    $body = [MdiSmokeNative]::Control($first, 1004)
    $form = [MdiSmokeNative]::GetParent($method)
    Assert-Check ($tree -ne [IntPtr]::Zero -and $method -ne [IntPtr]::Zero -and $url -ne [IntPtr]::Zero -and $headers -ne [IntPtr]::Zero -and $body -ne [IntPtr]::Zero) 'Each collection has a native tree and request editor'
    Assert-Check (-not [MdiSmokeNative]::IsWindowEnabled($send)) 'Send is disabled'
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($first, 1104)).Contains('Sending is not available')) 'Editor explains that sending is unavailable'

    $root = [MdiSmokeNative]::Send($tree, 0x110A, 0, 0) # TVM_GETNEXTITEM / TVGN_ROOT
    $request1 = [MdiSmokeNative]::Send($tree, 0x110A, 4, $root.ToInt64())
    $request2 = [MdiSmokeNative]::Send($tree, 0x110A, 1, $request1.ToInt64())
    Assert-Check ($request1 -ne [IntPtr]::Zero -and $request2 -ne [IntPtr]::Zero) 'Tree contains two selectable requests'

    # Build Unicode at runtime so Windows PowerShell 5.1 does not depend on a UTF-8 BOM.
    $unicode = 'caf' + [char]0xE9 + ' ' + [char]0x65E5 + [char]0x672C + ' ' + [char]::ConvertFromUtf32(0x1F680)
    $expectedUrl = 'http://127.0.0.1:8080/example?name=' + $unicode
    $expectedHeaders = "X-Repeat: one`r`nX-Repeat: two`r`nX-Unicode: $unicode`r`nUnfinished header  "
    $expectedBody = "{`r`n`t`"message`" : `"$unicode`",`r`n  `"items`": [ 1,  2 ]`r`n}`r`n`r`n  "
    [MdiSmokeNative]::Send($method, 0x14E, 1, 0) | Out-Null # CB_SETCURSEL / POST
    [MdiSmokeNative]::Send($form, 0x111, (0x10000 -bor 1000), $method.ToInt64()) | Out-Null
    [MdiSmokeNative]::ReplaceText($url, $expectedUrl)
    [MdiSmokeNative]::ReplaceText($headers, $expectedHeaders)
    [MdiSmokeNative]::ReplaceText($body, $expectedBody)
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request2.ToInt64()) | Out-Null # TVM_SELECTITEM / TVGN_CARET
    Assert-Check ([MdiSmokeNative]::Send($method, 0x147, 0, 0).ToInt64() -eq 0 -and [MdiSmokeNative]::ControlText($url) -eq '' -and [MdiSmokeNative]::ControlText($headers) -eq '' -and [MdiSmokeNative]::ControlText($body) -eq '') 'Second request starts with independent defaults'
    [MdiSmokeNative]::Send($method, 0x14E, 2, 0) | Out-Null # PUT
    [MdiSmokeNative]::Send($form, 0x111, (0x10000 -bor 1000), $method.ToInt64()) | Out-Null
    [MdiSmokeNative]::ReplaceText($url, 'http://127.0.0.1:8080/second')
    [MdiSmokeNative]::ReplaceText($headers, 'X-Second: yes')
    [MdiSmokeNative]::ReplaceText($body, 'second body')
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request1.ToInt64()) | Out-Null
    Assert-Check ([MdiSmokeNative]::Send($method, 0x147, 0, 0).ToInt64() -eq 1) 'Method survives request switching'
    Assert-Check ([MdiSmokeNative]::ControlText($url) -ceq $expectedUrl) 'Unicode URL survives request switching'
    Assert-Check ([MdiSmokeNative]::ControlText($headers) -ceq $expectedHeaders) 'Repeated headers, order and unfinished text survive switching'
    Assert-Check ([MdiSmokeNative]::ControlText($body) -ceq $expectedBody) 'Raw JSON, Unicode, whitespace and final newlines survive switching exactly'
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request2.ToInt64()) | Out-Null
    Assert-Check ([MdiSmokeNative]::Send($method, 0x147, 0, 0).ToInt64() -eq 2 -and [MdiSmokeNative]::ControlText($body) -ceq 'second body' -and [MdiSmokeNative]::ControlText($headers) -ceq 'X-Second: yes') 'Second request also retains its own edits'

    $longBody = ($unicode + "`r`n") * 6000
    [MdiSmokeNative]::ReplaceText($body, $longBody)
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request1.ToInt64()) | Out-Null
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request2.ToInt64()) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText($body) -ceq $longBody) 'Body larger than 32 KiB is not truncated'
    [MdiSmokeNative]::ReplaceText($body, '')
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request1.ToInt64()) | Out-Null
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request2.ToInt64()) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText($body) -ceq '') 'Clearing a body survives switching'

    $otherBody = [MdiSmokeNative]::Control($other, 1004)
    Assert-Check ([MdiSmokeNative]::ControlText($otherBody) -ceq '') 'Another collection starts with its own empty body'
    [MdiSmokeNative]::ReplaceText($otherBody, 'another collection')
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request1.ToInt64()) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText($body) -ceq $expectedBody) 'Editing another collection does not overwrite this collection'
    Assert-Check ([MdiSmokeNative]::ControlText($otherBody) -ceq 'another collection') 'Collection state remains independent'

    Assert-Check ([MdiSmokeNative]::GetNextDlgTabItem($form, $method, $false) -eq $url) 'Tab order moves from method to URL'
    Assert-Check ([MdiSmokeNative]::GetNextDlgTabItem($form, $url, $false) -eq $headers) 'Tab order skips disabled Send and reaches headers'
    Assert-Check ([MdiSmokeNative]::GetNextDlgTabItem($form, $headers, $false) -eq $body) 'Tab order moves from headers to raw body'

    [MdiSmokeNative]::MoveWindow($first, 10, 10, 1000, 680, $true) | Out-Null
    $large = [MdiSmokeNative]::Bounds($body)
    [MdiSmokeNative]::MoveWindow($first, 10, 10, 600, 430, $true) | Out-Null
    $small = [MdiSmokeNative]::Bounds($body)
    Assert-Check (($large.Bottom - $large.Top) -gt ($small.Bottom - $small.Top) -and ($small.Bottom - $small.Top) -gt 0) 'Body editor resizes with its collection window'
    Assert-Check ([MdiSmokeNative]::ControlText($body) -ceq $expectedBody) 'Resizing does not change raw text'

    # Temporarily show only this test instance to exercise the application's real
    # message loop and focus handling, rather than only inspecting tab styles.
    [MdiSmokeNative]::MoveWindow($Main, 20, 20, 1100, 760, $true) | Out-Null
    [MdiSmokeNative]::ShowWindow($Main, 4) | Out-Null
    [MdiSmokeNative]::Send([MdiSmokeNative]::Client($Main), 0x225, $first.ToInt64(), 0) | Out-Null
    [MdiSmokeNative]::Send($form, 0x28, $method.ToInt64(), 1) | Out-Null # WM_NEXTDLGCTL sets focus in the application's thread.
    foreach ($expected in @($url, $headers, $body, $tree, $method)) {
        $focused = [MdiSmokeNative]::Focus($Main)
        [MdiSmokeNative]::PostMessage($focused, 0x100, [IntPtr]9, [IntPtr]::Zero) | Out-Null
        $deadline = [DateTime]::UtcNow.AddSeconds(3)
        do {
            Start-Sleep -Milliseconds 25
            $actual = [MdiSmokeNative]::Focus($Main)
        } while ($actual -ne $expected -and [DateTime]::UtcNow -lt $deadline)
        Assert-Check ($actual -eq $expected) "Tab reaches control $([MdiSmokeNative]::GetDlgCtrlID($expected)) through the application message loop"
    }

    foreach ($expected in @($tree, $method)) {
        $focused = [MdiSmokeNative]::Focus($Main)
        [MdiSmokeNative]::PostMessage($focused, 0x100, [IntPtr]0x75, [IntPtr]::Zero) | Out-Null # F6
        $deadline = [DateTime]::UtcNow.AddSeconds(3)
        do {
            Start-Sleep -Milliseconds 25
            $actual = [MdiSmokeNative]::Focus($Main)
        } while ($actual -ne $expected -and [DateTime]::UtcNow -lt $deadline)
        Assert-Check ($actual -eq $expected) 'F6 switches the active splitter pane'
    }

    $splitter = [MdiSmokeNative]::GetParent($tree)
    $splitterBounds = [MdiSmokeNative]::Bounds($splitter)
    $treeBefore = [MdiSmokeNative]::Bounds($tree)
    $x = $treeBefore.Right - $splitterBounds.Left + 2
    $y = 80
    $point = ($y -shl 16) -bor $x
    $moved = ($y -shl 16) -bor ($x + 50)
    [MdiSmokeNative]::PostMessage($splitter, 0x201, [IntPtr]1, [IntPtr]$point) | Out-Null
    [MdiSmokeNative]::PostMessage($splitter, 0x200, [IntPtr]1, [IntPtr]$moved) | Out-Null
    [MdiSmokeNative]::PostMessage($splitter, 0x202, [IntPtr]::Zero, [IntPtr]$moved) | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 25
        $treeAfter = [MdiSmokeNative]::Bounds($tree)
    } while ($treeBefore.Right -eq $treeAfter.Right -and [DateTime]::UtcNow -lt $deadline)
    Assert-Check ($treeAfter.Right -gt $treeBefore.Right) 'Dragging the native splitter resizes the tree pane'
    Assert-Check ([MdiSmokeNative]::ControlText($body) -ceq $expectedBody) 'Splitter movement preserves the request body'

    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request2.ToInt64()) | Out-Null
    foreach ($character in @(65, 13, 66)) {
        [MdiSmokeNative]::PostMessage($body, 0x102, [IntPtr]$character, [IntPtr]::Zero) | Out-Null
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 25
        $typed = [MdiSmokeNative]::ControlText($body)
    } while ($typed -cne "A`r`nB" -and [DateTime]::UtcNow -lt $deadline)
    Assert-Check ($typed -ceq "A`r`nB") 'Typing Enter in the body inserts a line break'
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request1.ToInt64()) | Out-Null
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request2.ToInt64()) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText($body) -ceq "A`r`nB") 'Typed multiline body survives selection changes'
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request1.ToInt64()) | Out-Null

    if ($Screenshot) {
        Add-Type -AssemblyName System.Drawing
        [MdiSmokeNative]::UpdateWindow($Main) | Out-Null
        $rect = [MdiSmokeNative]::Bounds($Main)
        $bitmap = New-Object System.Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $dc = $graphics.GetHdc()
        try {
            if (-not [MdiSmokeNative]::PrintWindow($Main, $dc, 2)) { throw 'Window capture failed' }
        } finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
        try { $bitmap.Save([System.IO.Path]::GetFullPath($Screenshot), [System.Drawing.Imaging.ImageFormat]::Png) }
        finally { $bitmap.Dispose() }
    }
    [MdiSmokeNative]::Send([MdiSmokeNative]::Client($Main), 0x223, $first.ToInt64(), 0) | Out-Null
    [MdiSmokeNative]::ShowWindow($Main, 0) | Out-Null
}
