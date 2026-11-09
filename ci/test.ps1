# Runs the test-suite of the built trees on Windows without a display.
# Usage: ci\test.ps1 [Debug|Release]...   (default: Debug Release)
# Qt's bin directory must be on PATH (windeployqt is not needed for tests).
# Not verified yet (milestone M9).
param([string[]]$Configs = @('Debug', 'Release'))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:QT_QPA_PLATFORM = 'offscreen'
$qt = if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } elseif ($env:CMAKE_PREFIX_PATH) { $env:CMAKE_PREFIX_PATH } else { '' }
if ($qt) { $env:PATH = "$qt\bin;$env:PATH" }
foreach ($cfg in $Configs) {
    ctest --test-dir (Join-Path $root "build\$($cfg.ToLower())") --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
