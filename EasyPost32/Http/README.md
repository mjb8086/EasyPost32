# Standalone HTTP engine and workers — Slices 3–4

`HttpClient.h/.cpp` is a C++20/libcurl library with no MFC, window, resource,
JSON, persistence, or logging dependencies. `Execute` is blocking; call it only
in tests or on a worker. The GUI now uses `HttpJobs` to execute it asynchronously.
Each execution owns its easy handle and request-header list.
libcurl initialization is thread-safe and lazy, with process-lifetime cleanup.

## Build and test

From the main project directory, with Visual Studio 2026/v145 and Python 3.13:

```powershell
# Setup only: downloads native packages and creates a local Python environment.
powershell.exe -NoProfile -File .\tests\SetupHttp.ps1

# Normal offline workflow: build only the native engine/tests, start fixtures,
# wait for readiness, run the tests, propagate their exit code, and clean up.
powershell.exe -NoProfile -File .\tests\TestHttp.ps1 -Configuration Debug
powershell.exe -NoProfile -File .\tests\TestHttp.ps1 -Configuration Release
```

`-SkipBuild` runs against an already built test executable. The test target can
also be built directly from a Developer PowerShell:

```powershell
msbuild .\tests\HttpTests\HttpTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /nologo
```

`Http/Http.vcxproj` produces `x64/<configuration>/Http.lib`;
`tests/HttpTests/HttpTests.vcxproj` produces `HttpTests.exe` in the same directory,
with matching libcurl/zlib DLLs copied by vcpkg. Neither target requires MFC or
starts the GUI. Only x64 is configured for these new targets. The existing GUI
project and its configurations are retained in `EasyPost32.slnx`.

The checked-in vcpkg manifest pins its registry baseline and enables libcurl's
Windows Schannel TLS backend. JSON is used by the integration tests only.
Restore output is under ignored `build/vcpkg_installed`; Python packages are
pinned, including transitive dependencies, under `tests/LocalHttpServer` and
installed into ignored `build/test-venv`. `VCPKG_ROOT` can select an alternative
vcpkg installation. MSBuild never restores packages automatically, so a normal
build/test does not access package registries after setup.

## Request and response behavior

- Methods and ordered/repeated header pairs are passed to libcurl. Header
  names/methods must be HTTP tokens; CR, LF and NUL in header values are rejected.
  URLs must be HTTP(S), contain no embedded NUL, and already have the intended
  URL escaping. No variables, query encoding or JSON formatting are applied.
- An optional body distinguishes absent data from an explicitly empty body.
  Bytes are supplied with an explicit length, preserving UTF-8, line endings,
  whitespace and embedded NULs. An empty POST never reads stdin. HEAD with a
  nonempty request body is rejected. Set Content-Type explicitly when needed;
  the engine does not infer it from body content.
- Responses include status/reason, final URL, final ordered headers and raw
  header block, byte body, elapsed time, transferred body bytes, redirect count,
  and an optional transport error. HTTP 400/404/500 are ordinary responses.
  If `error` is set, any captured response body is partial/incomplete data,
  regardless of the HTTP status. Policy errors can have native code zero.
- The default total timeout is 30 seconds, with a 5-second connect timeout.
  A redirect chain shares that total budget. Underlying OS DNS/TLS operations
  can have their own scheduling behavior; this is not a hard real-time deadline.
- At most five automatic redirects are allowed, restricted to the initial
  scheme/host/port. Cross-origin, HTTPS-to-HTTP and non-HTTP(S) targets are blocked
  before forwarding custom credential headers or a body. The caller must
  explicitly request another origin. 307/308 retain method/body; 303 becomes
  GET except for HEAD, and POST becomes GET for 301/302.
  Transitions to GET drop stale payload headers (including a user-supplied
  Content-Length) along with the body.
- Captured bodies default to at most 16 MiB and headers to 256 KiB. Capture limits
  apply across redirect hops and interim header blocks, and callbacks abort the
  transfer when a limit is reached. They never throw into libcurl. Metadata
  reports bytes transferred, which can exceed bytes retained on a limit error.
- Text classification requires valid UTF-8 without binary control characters
  (Tab/CR/LF are allowed). Invalid UTF-8, embedded NULs and other binary content
  remain byte data; the GUI displays a safe placeholder. This is a
  conservative byte heuristic, not a MIME renderer or general encoding detector.
- Certificate-chain and hostname verification are enabled by default. Windows
  trust roots and Schannel's revocation checks remain intact. `caFile` can supply
  explicit PEM roots without modifying trust stores; `verifyTls=false` is an
  explicit caller choice, reported by `tlsVerificationEnabled` in the response.
  No revocation-bypass option is set. The GUI checkbox defaults to verification
  enabled, is locked during the transfer, and warns explicitly when disabled.
- Connections are direct: environment proxy variables are not used. Cookie
  persistence, advanced authentication, compressed-content decoding, upload
  streaming are not implemented. `HttpOptions::cancellation` accepts a C++20
  stop token; a pre-cancelled call performs no transfer and libcurl's progress
  callback interrupts active transfers with `TransportErrorKind::Cancelled`.
  OS DNS/TLS work can delay observation of cancellation. Calls can run concurrently;
  a handle is never shared between threads.
- No request/response content is logged or persisted. Diagnostics use generic
  libcurl error descriptions, without copying request data. `finalUrl`, headers
  and bodies in the returned value are sensitive caller data and must not be
  blindly logged or persisted.

## Worker ownership and UI completion

`HttpJobs` is independent of MFC. The application owns its registry of threads,
and each worker captures a value snapshot of the request/options and a shared
`HttpJob`. A job owns its stop source, result and atomic completion flag. A
release/acquire publication makes the immutable response safe to read after
`Done()`; callers must not inspect `Response()` earlier. Unexpected worker
exceptions leave a preallocated generic error, and never escape the thread.
Completed workers are joined and reaped during UI dispatch or before a new job.

Each collection owns per-request job references and cancels them on destruction.
Its editor uses a 100 ms Windows timer as the UI-thread completion dispatcher;
only that dispatcher updates views. Workers hold no `CWnd`, `CDocument`, HWND,
callback or document pointer and never post completion messages to child windows.
Closing a child destroys its timer immediately; the application retains and
eventually joins its worker safely. Application exit requests cancellation for
all jobs and joins them before libcurl cleanup. Requests can run independently
across selections/documents; the same request cannot be sent again until its
completion has been dispatched. No detached threads are used.

`RequestComposer` converts the editable draft to transport values without MFC.
It parses nonempty `Name: value` header lines, preserves repeats/order and
retains all body bytes. One conventional space after the colon is removed when
forming header values; the stored header text is unchanged. Incomplete lines
produce safe line-number diagnostics before sending. The HTTP core validates
tokens, protocols and injection safeguards independently.

The explicit length/body behavior follows the official
[POSTFIELDS documentation](https://curl.se/libcurl/c/CURLOPT_POSTFIELDS.html).
Redirect method behavior follows the HTTP status handling described in
[FOLLOWLOCATION](https://curl.se/libcurl/c/CURLOPT_FOLLOWLOCATION.html), with the
stricter origin restriction implemented here rather than delegating redirects.

## Loopback fixture and coverage

`tests/run_http_tests.py` reserves sockets on `127.0.0.1` with OS-assigned ports,
starts FastAPI/Uvicorn child processes without reload/access logging, and waits
for bounded readiness signals. It creates fresh local certificates and a local
CRL endpoint for Schannel, plus a reserved non-listening port and a deliberately
truncated TCP response. All certificates/keys are test-only; trust stores are
not modified. The runner removes temporary keys and terminates only its own
server processes in `finally`, including on test failure or timeout. The native
test executable rejects non-loopback fixture addresses.

The fixture has raw request inspection/echo, chosen statuses, delays, redirects,
bounded large/chunked bodies, large headers, binary bodies and invalid UTF-8.
The native tests prove exact GET/POST/PUT/PATCH/DELETE/OPTIONS requests, HEAD,
empty/binary POST, HTTP errors, refusal, timeout, invalid inputs, redirect
semantics/limits/origin blocking, body/header limits, classification, truncated
responses, default TLS rejection, explicit local trust, wrong-host rejection,
explicit validation disablement, independent concurrent requests, editor body
composition, safe header diagnostics, pre-cancellation, worker snapshots,
in-flight cancellation and safe release of document-owned job references.

There is no public endpoint dependency, account, product CLI, sample collection
file or test-time package restoration. The saved `.dp32.json` fixture collection
belongs to Slice 5. `tests/TestGuiHttp.ps1` now reuses these fixtures for real
GUI checks; `-Manual` prints addresses and keeps the fixtures alive until the
ordinary GUI instance exits. See the main README for exact manual requests.
Fixture/test binaries
and Python are development tools, not application startup dependencies.

## Verification

- x64 Debug and Release standalone engine/test builds passed with MSVC v145,
  C++20 and warning level 4; no compiler warnings were reported.
- Both `TestHttp.ps1` configurations passed 29 integration tests with exit code 0.
- Full `EasyPost32.slnx` Debug/Release builds passed, including the GUI.
- The existing `SmokeMdi.ps1 -Editor` Debug regression suite passed all 46 checks.
- The first trusted-CA test exposed missing revocation information in the
  fixture. A loopback-served CRL fixed the fixture while keeping Schannel
  revocation, chain and hostname verification enabled. No insecure TLS default
  or system certificate-store modification was introduced.
- Failed native tests returned nonzero and their fixtures/temporary keys were
  cleaned up; successful runs also left no fixture directories behind.
- Slice 4 adds six native composition/worker/cancellation checks, passing all
  35 native checks in both Debug and Release. Both full solution builds passed
  without reported compiler warnings/errors, and both GUI suites passed all 98
  checks. GUI checks now cover response display, concurrent
  editing, duplicate sends, TLS configuration and pending document/application
  closure. See the main README for verification commands and manual limitations.
- Public-service interoperability, other TLS backends, arbitrary proxy setups
  and non-Windows builds were not tested.
