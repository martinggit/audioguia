param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$AppRoot = Split-Path $PSScriptRoot -Parent
$CMake = Join-Path $AppRoot '.tools/w64devkit/bin/cmake.exe'
$CTest = Join-Path $AppRoot '.tools/w64devkit/bin/ctest.exe'
if (!(Test-Path $CMake)) { throw 'Run scripts/bootstrap.ps1 first.' }
$PreviousPath = $env:PATH
$env:PATH = "$AppRoot/.tools/w64devkit/bin;$PreviousPath"
Push-Location $AppRoot
try {
    & $CMake --preset windows-x86-release
    if ($LASTEXITCODE) { throw 'CMake configure failed' }
    & $CMake --build --preset windows-x86-release
    if ($LASTEXITCODE) { throw 'Build failed' }
    if (!$SkipTests) {
        & $CTest --preset windows-x86-release
        if ($LASTEXITCODE) { throw 'Tests failed' }
    }
} finally { Pop-Location; $env:PATH = $PreviousPath }
