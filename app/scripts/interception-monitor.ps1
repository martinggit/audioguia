$ErrorActionPreference = 'Stop'
$AppRoot = Split-Path $PSScriptRoot -Parent
$Tools = Join-Path $AppRoot '.tools/numpad'
$Arch = if ([Environment]::Is64BitOperatingSystem) { '64' } else { '32' }
$Ahk = Join-Path $Tools "AutoHotkey/AutoHotkey$Arch.exe"
$Monitor = Join-Path $Tools 'AutoHotInterception/AHK v2/Monitor.ahk'
if (!(Test-Path -LiteralPath $Ahk) -or !(Test-Path -LiteralPath $Monitor)) {
    throw 'Faltan las herramientas oficiales en app/.tools/numpad.'
}
Start-Process -FilePath $Ahk -ArgumentList ('"' + $Monitor + '"') -WorkingDirectory (Split-Path $Monitor -Parent)
