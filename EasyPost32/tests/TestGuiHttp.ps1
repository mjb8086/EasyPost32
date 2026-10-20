param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$SkipBuild,
    [switch]$Manual,
    [switch]$InteropOnly,
    [string]$Screenshot
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $SkipBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $install = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $install) { throw 'Visual Studio with MSBuild is required.' }
    & (Join-Path $install 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $root 'EasyPost32.slnx') /p:Configuration=$Configuration /p:Platform=x64 /m /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$python = Join-Path $root 'build\test-venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Run tests\SetupHttp.ps1 first.' }
$mode = if ($Manual) { '--manual' } else { '--gui' }
$arguments = @((Join-Path $PSScriptRoot 'run_http_tests.py'), $mode, '--executable', (Join-Path $root "x64\$Configuration\EasyPost32.exe"))
if ($Screenshot) { $arguments += @('--screenshot', $Screenshot) }
if ($InteropOnly) {
    if ($Manual) { throw 'InteropOnly and Manual are mutually exclusive.' }
    $arguments += '--interop-only'
}
& $python @arguments
exit $LASTEXITCODE
