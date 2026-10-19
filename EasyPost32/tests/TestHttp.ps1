param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPython = Join-Path $taskRoot 'build\test-venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $taskPython)) { throw 'Run tests\SetupHttp.ps1 first to restore test dependencies.' }
if (-not $SkipBuild) {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $taskInstallation) { throw 'Visual Studio/MSBuild is required.' }
    $taskMsbuild = Join-Path $taskInstallation 'MSBuild\Current\Bin\MSBuild.exe'
    & $taskMsbuild (Join-Path $PSScriptRoot 'HttpTests\HttpTests.vcxproj') "/p:Configuration=$Configuration" /p:Platform=x64 /m /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& $taskPython (Join-Path $PSScriptRoot 'run_http_tests.py') --executable (Join-Path $taskRoot "x64\$Configuration\HttpTests.exe")
exit $LASTEXITCODE
