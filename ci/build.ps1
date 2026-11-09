# Configures and builds trees under build\<config> on Windows (MSVC).
# Usage: ci\build.ps1 [Debug|Release]...   (default: Debug Release)
# Run from a "Developer PowerShell for VS 2022" (cl.exe and ninja on PATH).
# Qt: set CMAKE_PREFIX_PATH (or QT_ROOT_DIR, as install-qt-action does) to
# the Qt directory, e.g. C:\Qt\6.8.3\msvc2022_64.
# QUILLQT_WERROR=ON makes warnings errors. Not verified yet (milestone M9).
param([string[]]$Configs = @('Debug', 'Release'))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$prefix = if ($env:CMAKE_PREFIX_PATH) { $env:CMAKE_PREFIX_PATH } elseif ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { '' }
$werror = if ($env:QUILLQT_WERROR) { $env:QUILLQT_WERROR } else { 'OFF' }
foreach ($cfg in $Configs) {
    $dir = Join-Path $root "build\$($cfg.ToLower())"
    cmake -S $root -B $dir -G Ninja "-DCMAKE_BUILD_TYPE=$cfg" "-DCMAKE_PREFIX_PATH=$prefix" "-DQUILLQT_WERROR=$werror"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build $dir --parallel
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
