# Loaded by SmokeMdi.ps1 with private, ephemeral loopback fixture addresses.
function Test-HttpGui([IntPtr]$Main, [string]$HttpBase, [string]$TlsBase, [string]$RefusedBase, [string]$TruncatedBase, [string]$Screenshot) {
    foreach ($address in @($HttpBase, $TlsBase, $RefusedBase, $TruncatedBase)) {
        $uri = [Uri]$address
        if ($uri.Host -ne '127.0.0.1' -or $uri.Scheme -notin @('http', 'https')) { throw 'GUI fixtures must use loopback HTTP(S)' }
    }
    $client = [MdiSmokeNative]::Client($Main)
    $child = [MdiSmokeNative]::Send($client, 0x229, 0, 0)
    $method = [MdiSmokeNative]::Control($child, 1000)
    $url = [MdiSmokeNative]::Control($child, 1001)
    $send = [MdiSmokeNative]::Control($child, 1002)
    $headers = [MdiSmokeNative]::Control($child, 1003)
    $body = [MdiSmokeNative]::Control($child, 1004)
    $cancel = [MdiSmokeNative]::Control($child, 1005)
    $tls = [MdiSmokeNative]::Control($child, 1006)
    $summary = [MdiSmokeNative]::Control($child, 1007)
    $responseHeaders = [MdiSmokeNative]::Control($child, 1008)
    $responseBody = [MdiSmokeNative]::Control($child, 1009)
    $form = [MdiSmokeNative]::GetParent($method)
    $tree = [MdiSmokeNative]::Tree($child)
    $root = [MdiSmokeNative]::Send($tree, 0x110A, 0, 0)
    $request1 = [MdiSmokeNative]::Send($tree, 0x110A, 4, $root.ToInt64())
    $request2 = [MdiSmokeNative]::Send($tree, 0x110A, 1, $request1.ToInt64())

    function Set-GuiRequest([int]$MethodIndex, [string]$Address, [string]$RawBody = '', [string]$HeaderText = '') {
        [MdiSmokeNative]::Send($method, 0x14E, $MethodIndex, 0) | Out-Null
        [MdiSmokeNative]::Send($form, 0x111, (0x10000 -bor 1000), $method.ToInt64()) | Out-Null
        [MdiSmokeNative]::ReplaceText($url, $Address)
        [MdiSmokeNative]::ReplaceText($headers, $HeaderText)
        [MdiSmokeNative]::ReplaceText($body, $RawBody)
    }
    function Start-GuiSend {
        [MdiSmokeNative]::Send($send, 0xF5, 0, 0) | Out-Null # BM_CLICK
    }
    function Wait-GuiResponse {
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 25
            $text = [MdiSmokeNative]::ControlText($summary)
            if ([MdiSmokeNative]::IsWindowEnabled($send) -and $text -notmatch '^(Ready|Sending|Cancelling)') { return }
        } while ([DateTime]::UtcNow -lt $deadline)
        throw "GUI response did not complete: $text"
    }
    function Get-GuiHex([string]$Text) {
        return [BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($Text)).Replace('-', '').ToLowerInvariant()
    }

    Assert-Check ($summary -ne [IntPtr]::Zero -and $responseHeaders -ne [IntPtr]::Zero -and $responseBody -ne [IntPtr]::Zero) 'Response summary, headers and raw body are native controls'
    Assert-Check ([MdiSmokeNative]::Send($tls, 0xF0, 0, 0).ToInt64() -eq 1) 'TLS certificate and hostname validation are enabled by default'
    $unicode = 'caf' + [char]0xE9 + ' ' + [char]0x65E5 + [char]0x672C + ' ' + [char]::ConvertFromUtf32(0x1F680)
    $jsonBody = "{`r`n`t`"message`" : `"$unicode`",`r`n  `"numbers`" : [ 1,  2 ]`r`n}`r`n`r`n  "
    foreach ($methodIndex in 0..4) {
        $expectedMethod = @('GET', 'POST', 'PUT', 'PATCH', 'DELETE')[$methodIndex]
        $raw = if ($methodIndex -eq 0) { '' } else { $jsonBody }
        Set-GuiRequest $methodIndex "$HttpBase/inspect?literal=%2F+a&repeat=1&repeat=2" $raw "Content-Type: application/json`r`nX-Repeat: one`r`nX-Repeat: two"
        Start-GuiSend
        Wait-GuiResponse
        $result = [MdiSmokeNative]::ControlText($responseBody) | ConvertFrom-Json
        Assert-Check ($result.method -ceq $expectedMethod -and $result.query -ceq 'literal=%2F+a&repeat=1&repeat=2' -and $result.body_hex -ceq (Get-GuiHex $raw)) "GUI $expectedMethod sends exact method, query and raw UTF-8 body bytes"
        $repeated = @($result.headers | Where-Object { $_[0] -eq 'x-repeat' } | ForEach-Object { $_[1] })
        Assert-Check (($repeated -join ',') -ceq 'one,two' -and [MdiSmokeNative]::ControlText($responseHeaders).Contains("x-reply: one`r`nx-reply: two")) 'Repeated request and response headers remain ordered'
        Assert-Check ([MdiSmokeNative]::ControlText($summary) -match '^HTTP 200' -and [MdiSmokeNative]::ControlText($summary) -match '\d+ ms \| received \d+ bytes \| sent \d+ bytes') 'HTTP status, elapsed time and transfer sizes are displayed'
    }
    Set-GuiRequest 1 "$HttpBase/echo" $jsonBody 'Content-Type: application/json'
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($responseBody) -ceq $jsonBody -and [MdiSmokeNative]::ControlText($body) -ceq $jsonBody) 'Raw JSON response and request preserve whitespace, Unicode and final newlines'
    # Read-only native edits reject user input. EM_REPLACESEL intentionally
    # remains available to an application even on an ES_READONLY control.
    [MdiSmokeNative]::Send($responseBody, 0xB1, 0, -1) | Out-Null
    [MdiSmokeNative]::Send($responseBody, 0x102, 65, 0) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText($responseBody) -ceq $jsonBody) 'Response body is read only'

    foreach ($code in @(400, 500)) {
        Set-GuiRequest 0 "$HttpBase/status/$code"
        Start-GuiSend
        Wait-GuiResponse
        Assert-Check ([MdiSmokeNative]::ControlText($summary).StartsWith("HTTP $code ") -and [MdiSmokeNative]::ControlText($responseBody) -ceq "fixture status $code") "HTTP $code is displayed as an HTTP response with its body"
    }
    Set-GuiRequest 0 "$RefusedBase/ready"
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($summary).StartsWith('Transport error (connection)') -and [MdiSmokeNative]::ControlText($responseBody) -eq '') 'Connection failure is distinct from an HTTP error and clears stale body content'
    Set-GuiRequest 0 'not-a-url'
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($summary).StartsWith('Transport error (invalid request)')) 'Malformed URL is reported without crashing'
    Set-GuiRequest 0 "$HttpBase/ready" '' 'Unfinished synthetic header'
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($summary).StartsWith('Request not sent: Header line 1')) 'Incomplete headers fail before sending with an actionable diagnostic'
    Set-GuiRequest 0 "$HttpBase/binary"
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($responseBody).StartsWith('[Binary or non-UTF-8 response: 8 captured bytes.')) 'Binary bytes are replaced by a safe text placeholder'
    Set-GuiRequest 0 "$TruncatedBase/"
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($summary).Contains('captured content may be incomplete') -and [MdiSmokeNative]::ControlText($summary).Contains('HTTP 200') -and [MdiSmokeNative]::ControlText($responseBody) -ceq 'short') 'Interrupted responses display partial data with an explicit transport error'

    Set-GuiRequest 0 "$TlsBase/ready"
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($summary).StartsWith('Transport error (TLS)')) 'Untrusted TLS certificate fails under the default GUI setting'
    [MdiSmokeNative]::Send($tls, 0xF5, 0, 0) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText([MdiSmokeNative]::Control($child, 1104)).Contains('DISABLED')) 'Explicitly disabling TLS validation displays a warning'
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($summary).StartsWith('HTTP 200') -and [MdiSmokeNative]::ControlText($summary).Contains('TLS verification was DISABLED') -and [MdiSmokeNative]::ControlText($responseBody) -ceq 'ready') 'Explicit TLS override is honored and recorded visibly on its response'
    [MdiSmokeNative]::Send($tls, 0xF5, 0, 0) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText($summary).Contains('TLS verification was DISABLED')) 'Re-enabling TLS does not hide the previous response warning'

    Set-GuiRequest 1 "$HttpBase/slow-inspect" $jsonBody
    Start-GuiSend
    Assert-Check (-not [MdiSmokeNative]::IsWindowEnabled($send) -and [MdiSmokeNative]::IsWindowEnabled($cancel) -and [MdiSmokeNative]::ControlText($summary).StartsWith('Sending')) 'Pending request has a visible progress state and disables duplicate Send'
    Assert-Check (-not [MdiSmokeNative]::IsWindowEnabled($tls)) 'In-flight TLS setting stays visible and cannot be changed for the active transfer'
    [MdiSmokeNative]::Send($form, 0x111, 1002, $send.ToInt64()) | Out-Null
    Invoke-CommandId 32771 # Also attempt a duplicate through the menu command.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [MdiSmokeNative]::ReplaceText($body, 'edited while sending')
    Invoke-CommandId 0xE100
    Invoke-CommandId 0xE102
    $watch.Stop()
    Assert-Check ($watch.ElapsedMilliseconds -lt 1500 -and [MdiSmokeNative]::ControlText($body) -ceq 'edited while sending') 'Editing and opening/closing another collection stay responsive during a slow request'
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request2.ToInt64()) | Out-Null
    Assert-Check ([MdiSmokeNative]::ControlText($summary).StartsWith('Ready') -and [MdiSmokeNative]::ControlText($responseBody) -eq '') 'Switching requests does not show the other request response'
    Set-GuiRequest 0 "$HttpBase/ready"
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($responseBody) -ceq 'ready') 'Another request in the same collection can complete independently'
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request1.ToInt64()) | Out-Null
    Wait-GuiResponse
    $result = [MdiSmokeNative]::ControlText($responseBody) | ConvertFrom-Json
    Assert-Check ($result.body_hex -ceq (Get-GuiHex $jsonBody) -and [MdiSmokeNative]::ControlText($body) -ceq 'edited while sending') 'In-flight worker uses a snapshot and returns to the correct request after selection changes'
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request2.ToInt64()) | Out-Null
    Set-GuiRequest 0 "$HttpBase/send-count"
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($responseBody) -ceq '1') 'Fixture observed exactly one request despite repeated pending Send commands'
    [MdiSmokeNative]::Send($tree, 0x110B, 9, $request1.ToInt64()) | Out-Null

    Set-GuiRequest 0 "$HttpBase/delay/3000"
    Start-GuiSend
    [MdiSmokeNative]::Send($cancel, 0xF5, 0, 0) | Out-Null
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($summary).StartsWith('Transport error (cancelled)') -and -not [MdiSmokeNative]::IsWindowEnabled($cancel)) 'Cancel interrupts a pending transfer and returns the editor to a usable state'
    Start-GuiSend
    [MdiSmokeNative]::PostMessage($body, 0x100, [IntPtr]0x1B, [IntPtr]::Zero) | Out-Null
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($summary).StartsWith('Transport error (cancelled)')) 'Escape cancels through the application keyboard accelerator'
    for ($i = 0; $i -lt 3; ++$i) {
        Invoke-CommandId 0xE100
        $closing = [MdiSmokeNative]::Send($client, 0x229, 0, 0)
        [MdiSmokeNative]::ReplaceText([MdiSmokeNative]::Control($closing, 1001), "$HttpBase/delay/3000")
        Invoke-CommandId 32771
        $watch.Restart()
        Invoke-CommandId 0xE102
        $watch.Stop()
        Assert-Check (-not [MdiSmokeNative]::IsWindow($closing) -and $watch.ElapsedMilliseconds -lt 1000) 'Closing a pending collection releases its window promptly and safely'
    }
    Start-Sleep -Milliseconds 2200
    [MdiSmokeNative]::Send($client, 0x222, $child.ToInt64(), 0) | Out-Null
    Set-GuiRequest 0 "$HttpBase/status/200"
    Start-GuiSend
    Wait-GuiResponse
    Assert-Check ([MdiSmokeNative]::ControlText($responseBody) -ceq 'fixture status 200') 'Late completion after document closure cannot corrupt a surviving collection'

    [MdiSmokeNative]::MoveWindow($Main, 20, 20, 1200, 900, $true) | Out-Null
    [MdiSmokeNative]::ShowWindow($Main, 4) | Out-Null
    [MdiSmokeNative]::Send($client, 0x225, $child.ToInt64(), 0) | Out-Null
    $rightSplitter = [MdiSmokeNative]::GetParent($form)
    $before = [MdiSmokeNative]::Bounds($form)
    $splitBounds = [MdiSmokeNative]::Bounds($rightSplitter)
    $point = (($before.Bottom - $splitBounds.Top + 2) -shl 16) -bor 80
    $moved = (($before.Bottom - $splitBounds.Top + 82) -shl 16) -bor 80
    [MdiSmokeNative]::PostMessage($rightSplitter, 0x201, [IntPtr]1, [IntPtr]$point) | Out-Null
    [MdiSmokeNative]::PostMessage($rightSplitter, 0x200, [IntPtr]1, [IntPtr]$moved) | Out-Null
    [MdiSmokeNative]::PostMessage($rightSplitter, 0x202, [IntPtr]::Zero, [IntPtr]$moved) | Out-Null
    Start-Sleep -Milliseconds 200
    $after = [MdiSmokeNative]::Bounds($form)
    Assert-Check ($after.Bottom -gt $before.Bottom) 'Dragging the request/response splitter resizes the native panes'
    # Exercise Send through the active collection's Request menu command.
    Set-GuiRequest 0 "$HttpBase/inspect"
    Invoke-CommandId 32771
    Wait-GuiResponse
    foreach ($control in @($summary, $responseHeaders, $responseBody)) {
        $bounds = [MdiSmokeNative]::Bounds($control)
        $parentBounds = [MdiSmokeNative]::Bounds([MdiSmokeNative]::GetParent($control))
        Assert-Check ($bounds.Bottom -le $parentBounds.Bottom -and $bounds.Right -le $parentBounds.Right -and ($bounds.Bottom - $bounds.Top) -gt 30) 'Response controls fit the visible pane with readable dimensions'
    }
    if ($Screenshot) {
        Add-Type -AssemblyName System.Drawing
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
    # Leave a request pending so SmokeMdi also verifies application-wide shutdown.
    Set-GuiRequest 0 "$HttpBase/delay/3000"
    Start-GuiSend
}
