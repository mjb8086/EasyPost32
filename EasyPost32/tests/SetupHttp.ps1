$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (-not $taskInstallation) { throw 'Install Visual Studio with the C++ desktop toolchain first.' }
$taskVcpkg = if ($env:VCPKG_ROOT) { Join-Path $env:VCPKG_ROOT 'vcpkg.exe' } else { Join-Path $taskInstallation 'VC\vcpkg\vcpkg.exe' }
& $taskVcpkg install --triplet x64-windows "--x-manifest-root=$taskRoot" "--x-install-root=$taskRoot\build\vcpkg_installed"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$taskEnvironment = Join-Path $taskRoot 'build\test-venv'
if (-not (Test-Path -LiteralPath (Join-Path $taskEnvironment 'Scripts\python.exe'))) {
    python -m venv $taskEnvironment
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& (Join-Path $taskEnvironment 'Scripts\python.exe') -m pip install -r (Join-Path $PSScriptRoot 'LocalHttpServer\requirements.txt') --disable-pip-version-check
exit $LASTEXITCODE
